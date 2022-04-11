#include <algorithm>
#include <cmath>
#include "search_server.h"
#include "string_processing.h"

using namespace std;

void SearchServer::AddDocument(int document_id, const string_view data,
                               DocumentStatus status, const vector<int>& ratings) {

    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw invalid_argument("Invalid document_id"s);
    }

    const auto& [it, _] = documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status, static_cast<std::string>(data)});
    const string& document = it->second.data;

    const auto words = SplitIntoWordsNoStop(document);

    const double inv_word_count = 1.0 / words.size();
    for (const auto word : words) {
        word_to_document_freqs_[word][document_id] += inv_word_count;
        document_to_word_freqs_[document_id][word] += inv_word_count;
    }

    document_ids_.insert(document_id);
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(
        const execution::sequenced_policy&,
        const string_view raw_query, int document_id) const {

    if (document_ids_.count(document_id) == 0) {
        throw out_of_range("wrong id");
    }

    vector<string_view> matched_words;

    auto words_freqs = GetWordFrequencies(document_id);

    if (words_freqs.empty()) {
        return {matched_words, documents_.at(document_id).status};
    }

    const auto query = ParseQuery(raw_query);

    for (const auto& word_freqs : words_freqs) {
        if (find(query.minus_words.begin(),
                 query.minus_words.end(),
                 word_freqs.first) != query.minus_words.end()) {
            return {matched_words, documents_.at(document_id).status};
        }
    }

    for (const auto& word_freqs : words_freqs) {
        if (find(query.plus_words.begin(),
                 query.plus_words.end(),
                 word_freqs.first) != query.plus_words.end()) {
            matched_words.push_back(word_freqs.first);
        }
    }

    return {matched_words, documents_.at(document_id).status};
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(
        const execution::parallel_policy&,
        const string_view raw_query, int document_id) const {

    if (document_ids_.count(document_id) == 0) {
        throw out_of_range("wrong id");
    }

    vector<string_view> matched_words;

    auto words_freqs = GetWordFrequencies(document_id);

    if (words_freqs.empty()) {
        return {matched_words, documents_.at(document_id).status};
    }

    const auto query = ParseQuery(raw_query);

    for (const auto& word_freqs : words_freqs) {
        if (find(query.minus_words.begin(),
                query.minus_words.end(),
                word_freqs.first) != query.minus_words.end()) {
            return {matched_words, documents_.at(document_id).status};
        }
    }

    matched_words.reserve(query.plus_words.size());
    for_each(words_freqs.begin(), words_freqs.end(),
            [&query, &matched_words](auto& word_freqs) {
                if (find(query.plus_words.begin(),
                        query.plus_words.end(),
                        word_freqs.first) != query.plus_words.end()) {
                    matched_words.push_back(word_freqs.first);
                }
            }
    );

    return {matched_words, documents_.at(document_id).status};
}

bool SearchServer::IsStopWord(const string_view word) const {
    return stop_words_.count(word) > 0;
}

vector<string_view> SearchServer::SplitIntoWordsNoStop(const string_view text) const {
    vector<string_view> words;
    for (const auto word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw invalid_argument("Word "s + static_cast<std::string>(word) + " is invalid"s);
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

SearchServer::QueryWord SearchServer::ParseQueryWord(const string_view text) const {
    if (text.empty()) {
        throw invalid_argument("Query word is empty"s);
    }
    auto word = text;
    bool is_minus = false;
    if (word[0] == '-') {
        is_minus = true;
        word = word.substr(1);
    }
    if (word.empty() || word[0] == '-' || !IsValidWord(word)) {
        throw invalid_argument("Query word "s + static_cast<std::string>(text) + " is invalid");
    }

    return {word, is_minus, IsStopWord(word)};
}

SearchServer::Query SearchServer::ParseQuery(const string_view text) const {
    Query result;
    for (const auto word : SplitIntoWords(text)) {
        const auto query_word = SearchServer::ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.push_back(query_word.data);
            } else {
                result.plus_words.push_back(query_word.data);
            }
        }
    }

    return result;
}

// Existence required
double SearchServer::ComputeWordInverseDocumentFreq(const string_view word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}

std::set<int>::iterator SearchServer::begin() const {
    return document_ids_.begin();
}

std::set<int>::iterator SearchServer::end() const {
    return document_ids_.end();
}

const std::map<std::string_view, double> SearchServer::GetWordFrequencies(int document_id) const {
    return document_to_word_freqs_.at(document_id);
}

void SearchServer::RemoveDocument(int document_id) {
    auto it = document_ids_.find(document_id);
    if (it == document_ids_.end()) {return;}
    document_ids_.erase(it);
    documents_.erase(document_id);

    for(auto& [word, _] : document_to_word_freqs_.at(document_id)) {
        word_to_document_freqs_.at(word).erase(document_id);
    }

    document_to_word_freqs_.erase(document_id);
}

void SearchServer::RemoveDocument(const std::execution::sequenced_policy& policy, int document_id) {
    SearchServer::RemoveDocument(document_id);
}

void SearchServer::RemoveDocument(const std::execution::parallel_policy& policy, int document_id) {
    auto it = document_ids_.find(document_id);
    if (it == document_ids_.end()) {return;}
    document_ids_.erase(it);
    documents_.erase(document_id);

    std::vector<std::string_view> vec_doc_words(document_to_word_freqs_.at(document_id).size());
    std::transform(std::execution::par_unseq,
                   document_to_word_freqs_.at(document_id).begin(),
                   document_to_word_freqs_.at(document_id).end(),
                   vec_doc_words.begin(), [](auto& word_freq){
                        return word_freq.first;
                    });

    std::for_each(std::execution::par_unseq, vec_doc_words.begin(), vec_doc_words.end(),
                    [=, &document_id](auto word){
                        word_to_document_freqs_.at(word).erase(document_id);
                    });

    document_to_word_freqs_.erase(document_id);
}
