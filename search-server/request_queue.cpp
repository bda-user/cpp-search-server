#include "request_queue.h"

using namespace std;

RequestQueue::RequestQueue(const SearchServer& search_server)
    : search_server_(search_server) {}

// сделаем "обёртки" для всех методов поиска, чтобы сохранять результаты для нашей статистики
template <typename DocumentPredicate>
vector<Document> RequestQueue::AddFindRequest(const string& raw_query, DocumentPredicate document_predicate) {
    vector<Document> docs = search_server_.FindTopDocuments(raw_query, document_predicate);
    PushFindRequest(docs.empty());
    return docs;
}

vector<Document> RequestQueue::AddFindRequest(const string& raw_query, DocumentStatus status) {
    vector<Document> docs = search_server_.FindTopDocuments(raw_query, status);
    PushFindRequest(docs.empty());
    return docs;
}

vector<Document> RequestQueue::AddFindRequest(const string& raw_query) {
     vector<Document> docs = search_server_.FindTopDocuments(raw_query);
     PushFindRequest(docs.empty());
     return docs;
}

int RequestQueue::GetNoResultRequests() const {
    return requests_.back().count_nores;
}

void RequestQueue::PushFindRequest(bool nores){
    if(requests_.empty()) {
        QueryResult fresh = {nores, 0, 0};
        nores ? ++fresh.count_nores : ++fresh.count_res;
        requests_.push_back(fresh);
        return;
    }
    QueryResult last = requests_.back();
    bool decrease_count = (last.count_res + last.count_nores) >= min_in_day_ ? true : false;
    QueryResult fresh = {nores, last.count_res, last.count_nores};
    nores ? ++fresh.count_nores : ++fresh.count_res;
    if(decrease_count) {
        QueryResult first = requests_.front();
        requests_.pop_front();
        first.nores ? --fresh.count_nores : --fresh.count_res;
    }
    requests_.push_back(fresh);
}
