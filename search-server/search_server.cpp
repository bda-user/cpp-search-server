#include <algorithm>
#include <cmath>
#include "search_server.h"
#include "string_processing.h"

using namespace std;

SearchServer::SearchServer(const string& stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text))  // Invoke delegating constructor
                                                     // from string container
{
}

void SearchServer::AddDocument(int document_id, const string& document, DocumentStatus status, const vector<int>& ratings) {
    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw invalid_argument("Invalid document_id"s);
    }
    const auto words = SplitIntoWordsNoStop(document);

    const double inv_word_count = 1.0 / words.size();
    for (const string& word : words) {
        word_to_document_freqs_[word][document_id] += inv_word_count;
        document_to_word_freqs_[document_id][word] += inv_word_count;
    }
    documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status});
    document_ids_.insert(document_id);
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(
        const string& raw_query, int document_id) const {

    if (document_ids_.count(document_id) == 0) {
        throw std::out_of_range("wrong id");
    }

    vector<string_view> matched_words;

    //auto words_freqs = document_to_word_freqs_.at(document_id);
    auto words_freqs = GetWordFrequencies(document_id);

    if (words_freqs.empty()) {
        return {matched_words, documents_.at(document_id).status};
    }

    const auto query = ParseQuery(raw_query);
    //const auto query = ParseQueryVec(raw_query);

    for (const auto& word_freqs : words_freqs) {
        //if (query.minus_words.count(static_cast<std::string>(word_freqs.first))) {
        if (std::find(query.minus_words.begin(), query.minus_words.end(), static_cast<std::string>(word_freqs.first)) != query.minus_words.end()) {
            return {matched_words, documents_.at(document_id).status};
        }
    }

    for (const auto& word_freqs : words_freqs) {
        //if (query.plus_words.count(static_cast<std::string>(word_freqs.first))) {
        if (std::find(query.plus_words.begin(), query.plus_words.end(), static_cast<std::string>(word_freqs.first)) != query.plus_words.end()) {
            matched_words.push_back(word_freqs.first);
        }
    }

    return {matched_words, documents_.at(document_id).status};
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(
        const std::execution::parallel_policy& policy,
        const std::string& raw_query, int document_id) const {

    if (document_ids_.count(document_id) == 0) {
        throw std::out_of_range("wrong id");
    }

    std::vector<std::string_view> matched_words;

    //auto words_freqs = document_to_word_freqs_.at(document_id);
    auto words_freqs = GetWordFrequencies(document_id);

    if (words_freqs.empty()) {
        return {matched_words, documents_.at(document_id).status};
    }

    //const auto query = ParseQuery(raw_query);
    const auto query = ParseQueryVec(raw_query);

    for (const auto& word_freqs : words_freqs) {
//        if (query.minus_words.count(word_freqs.first)) {
        if (std::find(query.minus_words.begin(), query.minus_words.end(), word_freqs.first) != query.minus_words.end()) {
            return {matched_words, documents_.at(document_id).status};
        }
    }

    matched_words.reserve(query.plus_words.size());
    std::for_each(policy, words_freqs.begin(), words_freqs.end(),
        [&query, &matched_words, &policy](auto& word_freqs) {
//            if (query.plus_words.count(word_freqs.first) != 0) {
        if (std::find(query.plus_words.begin(), query.plus_words.end(), word_freqs.first) != query.plus_words.end()) {
                matched_words.push_back(static_cast<std::string>(word_freqs.first));
            }
        });

    return {matched_words, documents_.at(document_id).status};
}

bool SearchServer::IsStopWord(const string& word) const {
    return stop_words_.count(word) > 0;
}

vector<string> SearchServer::SplitIntoWordsNoStop(const string& text) const {
    vector<string> words;
    for (const string& word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw invalid_argument("Word "s + word + " is invalid"s);
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

SearchServer::QueryWord SearchServer::ParseQueryWord(const string& text) const {
    if (text.empty()) {
        throw invalid_argument("Query word is empty"s);
    }
    string word = text;
    bool is_minus = false;
    if (word[0] == '-') {
        is_minus = true;
        word = word.substr(1);
    }
    if (word.empty() || word[0] == '-' || !IsValidWord(word)) {
        throw invalid_argument("Query word "s + text + " is invalid");
    }

    return {word, is_minus, IsStopWord(word)};
}

SearchServer::Query SearchServer::ParseQuery(const string& text) const {
    Query result;
    for (const string& word : SplitIntoWords(text)) {
        const auto query_word = SearchServer::ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.insert(query_word.data);
            } else {
                result.plus_words.insert(query_word.data);
            }
        }
    }

    return result;
}

SearchServer::QueryVec SearchServer::ParseQueryVec(const std::string& text) const {
    QueryVec result;
    for (const std::string& word : SplitIntoWords(text)) {
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
double SearchServer::ComputeWordInverseDocumentFreq(const string& word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}

std::set<int>::iterator SearchServer::begin() const {
    return document_ids_.begin();
}

std::set<int>::iterator SearchServer::end() const {
    return document_ids_.end();
}

const std::map<std::string_view, double> SearchServer::GetWordFrequencies(int document_id) const {
    auto& word_freqs_ = document_to_word_freqs_.at(document_id);
    std::map<std::string_view, double> word_freqs;
    std::for_each(word_freqs_.begin(), word_freqs_.end(),
        [&word_freqs](auto& w_f_) {
            word_freqs[w_f_.first] = w_f_.second;
        });
    return word_freqs;
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

    std::vector<const std::string*> vec_doc_words(document_to_word_freqs_.at(document_id).size());
    std::transform(std::execution::par_unseq,
                   document_to_word_freqs_.at(document_id).begin(),
                   document_to_word_freqs_.at(document_id).end(),
                   vec_doc_words.begin(), [](auto& word_freq){
                        return &word_freq.first;
                    });

    std::for_each(std::execution::par_unseq, vec_doc_words.begin(), vec_doc_words.end(),
                    [=, &document_id](auto word){
                        word_to_document_freqs_.at(*word).erase(document_id);
                    });

    document_to_word_freqs_.erase(document_id);
}
