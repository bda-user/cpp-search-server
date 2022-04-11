#pragma once

#include <algorithm>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <stdexcept>
#include <execution>
#include <future>

#include "string_processing.h"
#include "document.h"
#include "concurrent_map.h"

const int MAX_RESULT_DOCUMENT_COUNT {5};
const double MAX_DELTA_RELEVANCE {1e-6};

class SearchServer {
public:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words);

    explicit SearchServer(const std::string_view stop_words_text)
        : SearchServer(SplitIntoWords(stop_words_text)) {
    }

    explicit SearchServer(const std::string& stop_words_text)
        : SearchServer(static_cast<std::string_view>(stop_words_text)) {
    }

    void AddDocument(int document_id, const std::string_view document, DocumentStatus status, const std::vector<int>& ratings);

    template <typename ExecutionPolicy, typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(
            ExecutionPolicy&& policy,
            const std::string_view raw_query, DocumentPredicate document_predicate) const;

    template <typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(
            const std::string_view raw_query, DocumentPredicate document_predicate) const {
        return FindTopDocuments(std::execution::seq, raw_query, document_predicate);
    }

    template <typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(
            ExecutionPolicy&& policy,
            const std::string_view raw_query, DocumentStatus status) const {
        return FindTopDocuments(policy, raw_query,
            [status](int document_id, DocumentStatus document_status, int rating) {
                return document_status == status;
            });
    }

    template <typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(
            ExecutionPolicy&& policy, const std::string_view raw_query) const {
        return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
    }

    std::vector<Document> FindTopDocuments(
            std::string_view raw_query, DocumentStatus status) const {
        return FindTopDocuments(std::execution::seq, raw_query, status);
    }

    std::vector<Document> FindTopDocuments(const std::string_view raw_query) const {
        return FindTopDocuments(std::execution::seq, raw_query, DocumentStatus::ACTUAL);
    }

    int GetDocumentCount() const;

    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(
            const std::execution::sequenced_policy&,
            const std::string_view raw_query, int document_id) const;

    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(
            const std::execution::parallel_policy&,
            const std::string_view raw_query, int document_id) const;

    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(
            const std::string_view raw_query, int document_id) const {
        return MatchDocument(std::execution::seq, raw_query, document_id);
    }

    std::set<int>::iterator begin() const;
    std::set<int>::iterator end() const;

    const std::map<std::string_view, double> GetWordFrequencies(int document_id) const;

    void RemoveDocument(int document_id);

    void RemoveDocument(const std::execution::sequenced_policy&, int document_id);

    void RemoveDocument(const std::execution::parallel_policy&, int document_id);

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
        std::string data;
    };
    const std::set<std::string_view, std::less<>> stop_words_;
    std::map<std::string_view, std::map<int, double>> word_to_document_freqs_;
    std::map<int, DocumentData> documents_;
    std::set<int> document_ids_;
    std::map<int, std::map<std::string_view, double>> document_to_word_freqs_;

    bool IsStopWord(const std::string_view word) const;

    static bool IsValidWord(const std::string_view word) {
           // A valid word must not contain special characters
           return std::none_of(word.begin(), word.end(), [](char c) {
               return c >= '\0' && c < ' ';
           });
    }

    std::vector<std::string_view> SplitIntoWordsNoStop(const std::string_view text) const;

    static int ComputeAverageRating(const std::vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        int rating_sum = 0;
        for (const int rating : ratings) {
            rating_sum += rating;
        }
        return rating_sum / static_cast<int>(ratings.size());
    }

    struct QueryWord {
        std::string_view data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(const std::string_view text) const;

    struct Query {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };

    Query ParseQuery(const std::string_view text) const;

    // Existence required
    double ComputeWordInverseDocumentFreq(const std::string_view word) const;

    template <typename DocumentPredicate> // CODE
    std::vector<Document> FindAllDocuments(
            const std::execution::sequenced_policy&,
            const Query& query, DocumentPredicate document_predicate) const;

    template <typename DocumentPredicate> // CODE
    std::vector<Document> FindAllDocuments(
            const std::execution::parallel_policy&,
            const Query& query, DocumentPredicate document_predicate) const;

    template <typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(
            const Query& query, DocumentPredicate document_predicate) const {
        return FindAllDocuments(std::execution::seq, query, document_predicate);
    }
};

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words)
    : stop_words_(MakeUniqueNonEmptyStrings(stop_words))  // Extract non-empty stop words
{
    if (!all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
        const char* s = "Some of stop words are invalid";
        throw std::invalid_argument(s);
    }
}

template <typename ExecutionPolicy, typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(
        ExecutionPolicy&& policy,
        const std::string_view raw_query, DocumentPredicate document_predicate) const {

    const auto query = ParseQuery(raw_query);

    std::vector<Document> matched_documents;

    // SEQ policy
    if constexpr (std::is_same_v<decltype(policy), decltype(std::execution::seq)&>) {

        matched_documents = FindAllDocuments(std::execution::seq, query, document_predicate);

        sort(std::execution::seq, matched_documents.begin(), matched_documents.end(),
             [](const Document& lhs, const Document& rhs) {

            if (std::abs(lhs.relevance - rhs.relevance) < MAX_DELTA_RELEVANCE) {
                return lhs.rating > rhs.rating;
            } else {
                return lhs.relevance > rhs.relevance;
            }

        });

        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }

    } // SEQ policy end
    else // PAR policy
    if constexpr (std::is_same_v<decltype(policy), decltype(std::execution::par)&>) {
        matched_documents = FindAllDocuments(std::execution::par, query, document_predicate);

        sort(std::execution::par_unseq, matched_documents.begin(), matched_documents.end(),
             [](const Document& lhs, const Document& rhs) {

            if (std::abs(lhs.relevance - rhs.relevance) < MAX_DELTA_RELEVANCE) {
                return lhs.rating > rhs.rating;
            } else {
                return lhs.relevance > rhs.relevance;
            }

        });

        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
    }

    return matched_documents;
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(
        const std::execution::sequenced_policy&,
        const Query& query, DocumentPredicate document_predicate) const {

    std::map<int, double> document_to_relevance;

    for (const auto word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
        for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
            const auto& document_data = documents_.at(document_id);
            if (document_predicate(document_id, document_data.status, document_data.rating)) {
                document_to_relevance[document_id] += term_freq * inverse_document_freq;
            }
        }
    }

    for (const auto word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
            document_to_relevance.erase(document_id);
        }
    }

    std::vector<Document> matched_documents;
    for (const auto [document_id, relevance] : document_to_relevance) {
        matched_documents.push_back({document_id, relevance, documents_.at(document_id).rating});
    }

    return matched_documents;
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(
        const std::execution::parallel_policy&,
        const Query& query, DocumentPredicate document_predicate) const {

    ConcurrentMap<int, double> document_to_relevance_cm(16);

    auto f_plus_words = [=, &document_to_relevance_cm,
            &query, &document_predicate] (size_t begin, size_t end) {

        const auto it_begin = std::next(query.plus_words.begin(), begin);
        const auto it_end = std::next(query.plus_words.begin(), end);

        std::for_each(std::execution::par_unseq, it_begin, it_end,
            [=, &document_to_relevance_cm, &document_predicate](auto& word){

                if (word_to_document_freqs_.count(word) != 0) {
                    const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
                    for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                        const auto& document_data = documents_.at(document_id);
                        if (document_predicate(document_id, document_data.status, document_data.rating)) {
                            document_to_relevance_cm[document_id].ref_to_value +=
                                term_freq * inverse_document_freq;
                        }
                    }
                }

            }
        );

    };

    constexpr size_t THREAD_COUNT = 8;
    size_t interval = query.plus_words.size() / THREAD_COUNT;
    std::vector<std::future<void>> futures;
    size_t begin = 0;
    size_t end = interval;
    for (size_t i = 0; i < THREAD_COUNT; ++i) {
        futures.push_back(std::async(f_plus_words, begin, end));
        begin = end;
        if(i == THREAD_COUNT - 2) {
            end = query.plus_words.size();
        } else {
            end += interval;
        }
    }
    for (size_t i = 0; i < THREAD_COUNT; ++i) {
        futures[i].get();
    }

    std::map<int, double> document_to_relevance =
            document_to_relevance_cm.BuildOrdinaryMap();

    for (const auto& word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
            document_to_relevance.erase(document_id);
        }
    }

    std::vector<Document> matched_documents;
    for (const auto [document_id, relevance] : document_to_relevance) {
        matched_documents.push_back({document_id, relevance, documents_.at(document_id).rating});
    }

    return matched_documents;
}
