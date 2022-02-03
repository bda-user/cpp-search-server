// search_server_s1_t2_v2.cpp

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <cassert>
#include <numeric>

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT {5};
const double MAX_DELTA_RELEVANCE {1e-6};  // add after review

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result;
    cin >> result;
    ReadLine();
    return result;
}

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }

    return words;
}

struct Document {
    int id;
    double relevance;
    int rating;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

class SearchServer {
public:
    void SetStopWords(const string& text) {
        for (const string& word : SplitIntoWords(text)) {
            stop_words_.insert(word);
        }
    }

    void AddDocument(int document_id, const string& document, DocumentStatus status, const vector<int>& ratings) {
        const vector<string> words = SplitIntoWordsNoStop(document);
        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
        documents_.emplace(document_id,
            DocumentData{
                ComputeAverageRating(ratings),
                status
            });
    }

    template <typename KeyMapper>
    vector<Document> FindTopDocuments(const string& raw_query, KeyMapper key_mapper) const {
        const Query query = ParseQuery(raw_query);
        auto matched_documents = FindAllDocuments(query, key_mapper);

        sort(matched_documents.begin(), matched_documents.end(),
             [](const Document& lhs, const Document& rhs) {
                if (abs(lhs.relevance - rhs.relevance) < MAX_DELTA_RELEVANCE) {  // update after review
                    return lhs.rating > rhs.rating;
                } else {
                    return lhs.relevance > rhs.relevance;
                }
             });

        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
        return matched_documents;
    }

    vector<Document> FindTopDocuments(const string& raw_query, DocumentStatus status) const {
        return FindTopDocuments(raw_query,
                [status](int document_id, DocumentStatus doc_status, int rating) {
                    return status == doc_status;
                });
    }

    vector<Document> FindTopDocuments(const string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }

    int GetDocumentCount() const {
        return documents_.size();
    }

    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query, int document_id) const {
        const Query query = ParseQuery(raw_query);
        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }
        return {matched_words, documents_.at(document_id).status};
    }

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };

    set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;

    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }
        return words;
    }

    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        return static_cast<int> (
                    accumulate(ratings.begin(), ratings.end(), 0) /  // update after review
                    static_cast<int>(ratings.size())
               );
    }

    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(string text) const {
        bool is_minus = false;
        // Word shouldn't be empty
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        return {
            text,
            is_minus,
            IsStopWord(text)
        };
    }

    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };

    Query ParseQuery(const string& text) const {
        Query query;
        for (const string& word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                } else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return query;
    }

    // Existence required
    double ComputeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }

    template <typename KeyMapper>
    vector<Document> FindAllDocuments(const Query& query, KeyMapper key_mapper) const {
        map<int, double> document_to_relevance;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                if (key_mapper(
                        document_id,
                        documents_.at(document_id).status,
                        documents_.at(document_id).rating
                    )
                ) {
                    document_to_relevance[document_id] += term_freq * inverse_document_freq;
                }
            }
        }

        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }

        vector<Document> matched_documents;
        for (const auto [document_id, relevance] : document_to_relevance) {
            matched_documents.push_back({
                document_id,
                relevance,
                documents_.at(document_id).rating
            });
        }
        return matched_documents;
    }
};

// testing framework

ostream& operator<<(ostream& os, const vector<int>& v) {
    bool is_first = true;
    string s = ""s;
    for (const auto& e : v) {
        if(is_first) {
            is_first = false;
            s += "["s + to_string(e);
        } else {
            s +=  ", "s + to_string(e);
        }
    }
    if(s != ""s) {
       s += "]"s;
    }
    os << s;
    return os;
}

template <typename T, typename U>
void AssertEqualImpl(const T& t, const U& u, const string& t_str, const string& u_str, const string& file,
                     const string& func, unsigned line, const string& hint) {
    if (t != u) {
        cerr << boolalpha;
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT_EQUAL("s << t_str << ", "s << u_str << ") failed: "s;
        cerr << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

#define ASSERT_EQUAL(a, b) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_EQUAL_HINT(a, b, hint) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, (hint))

void AssertImpl(bool value, const string& expr_str, const string& file, const string& func, unsigned line,
                const string& hint) {
    if (!value) {
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT("s << expr_str << ") failed."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

#define ASSERT(expr) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_HINT(expr, hint) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, (hint))

template <typename T>
void RunTestImpl(const T& t, const string& func) {
    t();
    cerr << func << " OK"s << endl;
}

#define RUN_TEST(func) RunTestImpl((func), #func)

// -------- Начало модульных тестов поисковой системы ----------

// Тест проверяет, что поисковая система исключает стоп-слова при добавлении документов

void TestExcludeStopWordsFromAddedDocumentContent() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};

    // Сначала убеждаемся, что поиск слова, не входящего в список стоп-слов,
    // находит нужный документ
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL(found_docs.size(), 1);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, doc_id);
    }

    // Затем убеждаемся, что поиск этого же слова, входящего в список стоп-слов,
    // возвращает пустой результат
    {
        SearchServer server;
        server.SetStopWords("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT(server.FindTopDocuments("in"s).empty());
    }
}

//Разместите код остальных тестов здесь

void TestAddDocumentContent(){
    {
        SearchServer server;
        ASSERT_EQUAL(server.GetDocumentCount(), 0);
        server.AddDocument(0, "белый кот и модный ошейник"s,        DocumentStatus::ACTUAL, {8, -3});
        server.AddDocument(1, "пушистый кот пушистый хвост"s,       DocumentStatus::ACTUAL, {7, 2, 7});
        server.AddDocument(2, "ухоженный пес выразительные глаза"s, DocumentStatus::ACTUAL, {5, -12, 2, 1});
        ASSERT_EQUAL(server.GetDocumentCount(), 3);
        ASSERT_EQUAL(server.FindTopDocuments("кот"s).size(), 2);
        ASSERT_EQUAL(server.FindTopDocuments("пес"s)[0].id, 2);
    }
}

void TestMinusWordsFromAddedDocumentContent() {
    {
        SearchServer server;
        server.AddDocument(0, "cat in the city"s, DocumentStatus::ACTUAL, {1, 2, 3});
        server.AddDocument(1, "dog in the city"s, DocumentStatus::ACTUAL, {-1, 2, 3});
        ASSERT_EQUAL(server.FindTopDocuments("city"s).size(), 2);
        ASSERT_EQUAL(server.FindTopDocuments("city -cat"s).size(), 1);
        ASSERT(server.FindTopDocuments("city -cat -dog"s).empty());
    }
}

void TestMatchDocumentFromAddedDocumentContent() {
    {
        SearchServer server;
        server.AddDocument(0, "dog in the city"s, DocumentStatus::ACTUAL, {-1, 2, 3});
        //tuple<vector<string>, DocumentStatus>
        auto [ v_s, sta ] = server.MatchDocument("cit"s, 0);
        ASSERT_EQUAL(v_s.size(), 0);
        auto [ v_s2, sta2 ] = server.MatchDocument("city dog"s, 0);
        ASSERT_EQUAL(v_s2.size(), 2);
        auto [ v_s3, sta3 ] = server.MatchDocument("city -dog"s, 0);
        ASSERT_EQUAL(v_s3.size(), 0);
    }
}

void TestCalcRelevanceDocumentsFromAddedDocumentContent() {
    {
        SearchServer server;
        server.AddDocument(0, "белый кот и модный ошейник"s,        DocumentStatus::ACTUAL, {8, -3});
        server.AddDocument(1, "пушистый кот пушистый хвост"s,       DocumentStatus::ACTUAL, {7, 2, 7});
        server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, {5, -12, 2, 1});
        const auto found_docs = server.FindTopDocuments("ухоженный кот"s);
        ASSERT(abs(found_docs[0].relevance - 0.274653) < MAX_DELTA_RELEVANCE);  // update after review
        ASSERT(abs(found_docs[1].relevance - 0.101366) < MAX_DELTA_RELEVANCE);  // update after review
        ASSERT(abs(found_docs[2].relevance - 0.081093) < MAX_DELTA_RELEVANCE);  // update after review
    }
}

void TestCalcRatingDocumentsFromAddedDocumentContent() {
    {
        SearchServer server;
        server.AddDocument(0, "белый кот и модный ошейник"s,        DocumentStatus::ACTUAL, {8, -3});
        server.AddDocument(1, "пушистый кот пушистый хвост"s,       DocumentStatus::ACTUAL, {7, 2, 7});
        server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, {5, -12, 2, 1});
        const auto found_docs = server.FindTopDocuments("ухоженный кот"s);
        ASSERT_EQUAL(found_docs[0].rating, -1);
        ASSERT_EQUAL(found_docs[1].rating, 5);
        ASSERT_EQUAL(found_docs[2].rating, 2);
    }
}

void TestSortDocumentsByRelevanceFromAddedDocumentContent(){
    {
        SearchServer server;
        server.AddDocument(0, "белый кот и модный ошейник"s,        DocumentStatus::ACTUAL, {8, -3});
        server.AddDocument(1, "пушистый кот пушистый хвост"s,       DocumentStatus::ACTUAL, {7, 2, 7});
        server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, {5, -12, 2, 1});
        vector<int> found_docs_id;
        for(const auto& doc : server.FindTopDocuments("ухоженный кот"s)) {
            found_docs_id.push_back(doc.id);
        }
        vector<int> etalon_docs_id{2, 1, 0};
        ASSERT_EQUAL(found_docs_id, etalon_docs_id);
    }
}

void TestStatusDocumentsFromAddedDocumentContent() {
    {
        SearchServer server;
        server.AddDocument(0, "ухоженный пёс выразительные"s, DocumentStatus::BANNED, {5, -12, 2, 1});
        server.AddDocument(1, "ухоженный выразительные"s, DocumentStatus::IRRELEVANT, {5, -12, 2, 1});
        server.AddDocument(2, "ухоженный пёс"s, DocumentStatus::REMOVED, {5, -12, 2, 1});
        server.AddDocument(3, "ухоженный пёс и кот"s, DocumentStatus::ACTUAL, {5, -12, 2, 1});
        server.AddDocument(4, "ухоженный пёс и коты"s, DocumentStatus::ACTUAL, {5, -12, 2, 1});
        ASSERT_EQUAL(server.FindTopDocuments("ухоженный"s, DocumentStatus::BANNED).size(), 1);
        ASSERT_EQUAL(server.FindTopDocuments("ухоженный"s, DocumentStatus::IRRELEVANT).size(), 1);
        ASSERT_EQUAL(server.FindTopDocuments("ухоженный"s, DocumentStatus::REMOVED).size(), 1);
        ASSERT_EQUAL(server.FindTopDocuments("ухоженный"s, DocumentStatus::ACTUAL).size(), 2); // add after review
    }
}

void TestFindDocumentsByPredicateFromAddedDocumentContent(){
    {
        SearchServer server;
        server.AddDocument(0, "белый кот и модный ошейник"s,        DocumentStatus::ACTUAL, {8, -3});
        server.AddDocument(1, "пушистый кот пушистый хвост"s,       DocumentStatus::ACTUAL, {7, 2, 7});
        server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, {5, -12, 2, 1});
        ASSERT_EQUAL(server.FindTopDocuments(
                    "кот"s,
                    [](int id, DocumentStatus status, int rating) {
                        return rating > 0;
                    }
                    ).size(), 2);
    }
}

// Функция TestSearchServer является точкой входа для запуска тестов
void TestSearchServer() {
    RUN_TEST(TestExcludeStopWordsFromAddedDocumentContent);
    // Не забудьте вызывать остальные тесты здесь
    RUN_TEST(TestAddDocumentContent); // add after review
    RUN_TEST(TestMinusWordsFromAddedDocumentContent);
    RUN_TEST(TestMatchDocumentFromAddedDocumentContent);
    RUN_TEST(TestSortDocumentsByRelevanceFromAddedDocumentContent);  // add after review
    RUN_TEST(TestFindDocumentsByPredicateFromAddedDocumentContent);  // add after review
    RUN_TEST(TestCalcRelevanceDocumentsFromAddedDocumentContent);  // update split after review
    RUN_TEST(TestCalcRatingDocumentsFromAddedDocumentContent);  // update split after review
    RUN_TEST(TestStatusDocumentsFromAddedDocumentContent); // update after review
}

// --------- Окончание модульных тестов поисковой системы -----------


int main() {
    TestSearchServer();
    // Если вы видите эту строку, значит все тесты прошли успешно
    cout << "Search server testing finished"s << endl;
}
