#ifndef SBD2_STORAGE_H
#define SBD2_STORAGE_H

#include "BTree.h"
#include "Record.h"
#include "CommonHeader.h"
#include <fstream>
#include <string>
#include <vector>
#include <tuple>
#include <memory>

namespace BTreeNS {
    class BTreeNode;
}
class Storage {
    std::string filename;
    int pagesize, filesize, pages;
    std::fstream file;

public:
    static int reads, writes;

    Storage(std::string _name, int _pagesize);

    ~Storage();

    void clear();

    std::shared_ptr<bytearray> getPage(int index);

    void flush();

    void setPage(int index, std::shared_ptr<bytearray> data);

    int getPageCount();
};

class BTreeStorage {
    std::shared_ptr<Storage> storage;
//    std::vector<int> freenodes;
    std::shared_ptr<bytearray> page;
    std::vector<std::tuple<int, std::shared_ptr<BTreeNS::BTreeNode>>> btreecache;
public:
    int nextnode;

    BTreeStorage(std::string name);

    ~BTreeStorage() = default;

    void clear();

    std::shared_ptr<BTreeNS::BTreeNode> get(int index);

    void set(int index, std::shared_ptr<BTreeNS::BTreeNode> node);

    void addToCache(int index, std::shared_ptr<BTreeNS::BTreeNode> node);

    void removeFromCache(int index);

    std::shared_ptr<BTreeNS::BTreeNode> newNode();

    void flush();
};

class DataStorage {
    std::shared_ptr<Storage> storage;

public:
    int nextpage, nextoffset;

    DataStorage(std::string name);

    void clear();

    std::shared_ptr<Record> get(int pageNo, int offset);

    // TODO: poprawic to
    void set(int pageNo, int offset, std::shared_ptr<Record> rec);

    std::tuple<int, int> insert(std::shared_ptr<Record> rec);

    void flush();
};

#endif //SBD2_STORAGE_H
