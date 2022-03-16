#include "remove_duplicates.h"

void RemoveDuplicates(SearchServer& search_server) {
    std::set<int> del_ids;
    for (int doc_id1 : search_server) {
        auto m1 = search_server.GetWordFrequencies(doc_id1);
        for (int doc_id2 : search_server) {
            if(doc_id1 == doc_id2) continue;
            auto m2 = search_server.GetWordFrequencies(doc_id2);
            if(m1.size() != m2.size()) continue;
            bool find = true;
            for(auto it1 = m1.begin(), it2 = m2.begin(); it1 != m1.end(); it1++, it2++) {
                if(it1->first != it2->first) {
                    find = false;
                    break;
                }
            }
            if (find) {
                del_ids.emplace(doc_id1 < doc_id2 ? doc_id2 : doc_id1);
            }
        }
    }

    for(auto id : del_ids) {
        search_server.RemoveDocument(id);
        std::cout << "Found duplicate document id " << id << std::endl;
    }
}
