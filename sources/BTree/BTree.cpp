#include "../../headers/BTree.h"
#include "../../headers/Storage.h"

using namespace BTreeNS;

BTree::BTree(std::string _name) {
    metadata = std::vector<int>(7);
    name = _name;
    btree = std::make_shared<BTreeStorage>(name);
    data = std::make_shared<DataStorage>(name);
    auto metadataname = name;
    metadataname.append(".meta");
    metadataStorage = std::make_shared<Storage>(metadataname, 7 * sizeof(int));
    loadMetadata();
}

BTree::~BTree() {
    flushMetadata();
}

void BTree::flush() {
    flushMetadata();
    btree->flush();
    data->flush();
}

void BTree::printData() {
    int size = metadata[0];
    for (int i = 0; size > 0; i++) {
        auto rec = get(i);
        if (rec != nullptr) {
            rec->print();
            size--;
        }
    }
};

void BTree::loadMetadata() {
    auto str = name.append(".meta");
    std::ifstream in(str, std::ifstream::ate | std::ifstream::binary);
    bool size = in.tellg();
    in.close();
    if (size > 0) {
        // load metadata
        auto meta = metadataStorage->getPage(0);
        memcpy(reinterpret_cast<char *>(&metadata[0]), meta->arr, 7 * sizeof(int));
    } else {
        auto node = createTree();

        flushMetadata();
    }
}

void BTree::flushMetadata() {
    metadata[4] = data->nextpage; // data_next_page
    metadata[5] = data->nextoffset; // data_next_offset
    metadata[6] = btree->nextnode; // btree_next_node
    // zapisywanie metadanych do bytearray;
    std::shared_ptr<bytearray> meta = std::make_shared<bytearray>(7 * sizeof(int));
    memcpy(meta->arr, reinterpret_cast<char *>(&metadata[0]), 7 * sizeof(int));
    metadataStorage->setPage(0, meta);
}

void BTree::syncMetadataStorage() {
    data->nextpage = metadata[4]; // data_next_page
    data->nextoffset = metadata[5]; // data_next_offset
    btree->nextnode = metadata[6]; // btree_next_node
}

std::shared_ptr<BTreeNode> BTree::createTree() {
    metadata[0] = 0; // elements
    metadata[1] = 1; // nodes
    metadata[2] = 1; // height
    metadata[3] = 0; // root
    metadata[4] = 0; // data_next_page
    metadata[5] = 0; // data_next_offset
    metadata[6] = 0; // btree_next_node
    syncMetadataStorage();
    btree->clear();
    data->clear();
    auto root = btree->newNode();
    root->leaf = true;
    metadata[3] = root->index;

    return root;
}

std::shared_ptr<BTreeNode> BTree::newNode() {
    metadata[1]++; // nodes
    return btree->newNode();
}

void BTree::cacheUpdate(std::shared_ptr<BTreeNode> node) {
    btree->get(node->index);
}

std::tuple<std::shared_ptr<BTreeNode>, bool> BTree::getNodeForKey(int key, bool test) {
    auto node_idx = metadata[3]; // root;
    if (node_idx == -1)
        if (test)
            return std::tuple<std::shared_ptr<BTreeNode>, bool>(createTree(), false);
        else
            return std::tuple<std::shared_ptr<BTreeNode>, bool>(nullptr, false);
    std::shared_ptr<BTreeNode> node = btree->get(node_idx);
    while (true) {
        auto cell = node->getCellByKey(key);
        if (cell == nullptr) {
            return std::tuple<std::shared_ptr<BTreeNode>, bool>(node, false);
        }
        if (cell->getKey() == key)
            return std::tuple<std::shared_ptr<BTreeNode>, bool>(node, true);
        else if (cell->child < MAX_SIZE) {
            node = btree->get(cell->child);
            node_idx = cell->child;
            // flush?
        } else
            return std::tuple<std::shared_ptr<BTreeNode>, bool>(node, false);
    }
}

void BTree::insertCellIntoNode(std::shared_ptr<BTreeNodeCell> cell, std::shared_ptr<BTreeNode> node) {
    cacheUpdate(node);
    if (node->size() > BTREE_2D) {
        flush();
        throw "Not enough space in node!";
    }
    node->insert(cell);
    // overflow
    if (node->size() > BTREE_2D) {
        bool status = compensationPhaseOne(node);
        cacheUpdate(node);
        if (!status)
            splitNode(node);
    }
}

std::vector<int> BTree::getSiblings(std::shared_ptr<BTreeNode> node) {
    if (node->parent == MAX_SIZE) // if node is root
        return std::vector<int>();
    std::vector<int> siblings;
    auto parentNode = btree->get(node->parent);
    if (parentNode->cells.size() < 2)
        return std::vector<int>();
    else if (parentNode->cells[0]->child == node->index) // rightmost index;
        siblings.push_back(parentNode->cells[1]->child);
    else if (parentNode->cells.back()->child == node->index)  // leftmost TODO: check correctness
        siblings.push_back(parentNode->cells.end()[-2]->child);
    else {
        for (int i = 0; i < parentNode->cells.size(); i++) {
            if (node->index == parentNode->cells[i]->child) {
                siblings.push_back(parentNode->cells[i - 1]->child);
                siblings.push_back(parentNode->cells[i + 1]->child);
                break;
            }
        }
    }
    return siblings;
}

bool BTree::compensationPhaseOne(std::shared_ptr<BTreeNode> node) {
    auto siblings = getSiblings(node);
    for (auto item : siblings) {
        if (item == MAX_SIZE)
            continue;
        auto node2 = btree->get(item);
        if (BTREE_2D <= node->size() + node2->size() && node->size() + node2->size() <= BTREE_2D * 2) {
            compensationPhaseTwo(node, node2);
            return true;
        }
    }
    return false;
}

void BTree::compensationPhaseTwo(std::shared_ptr<BTreeNode> node1, std::shared_ptr<BTreeNode> node2) {
    cacheUpdate(node1);
    cacheUpdate(node2);

    if (node1->size() == node2->size()) {
        return;
    }

    auto parent = btree->get(node1->parent);
    int target_length = (node1->size() + node2->size()) / 2;

    if (target_length > BTREE_2D)
        throw "Target length is too big!";

    if (node1->size() > 0 && node2->size() && node1->cells[0]->getKey() > node2->cells[0]->getKey()) {
        std::swap(node1, node2);
    } else if (node1->size() == 0 || node2->size() == 0) {
        auto p1_cell = parent->getCellByChild(node1->index);
        auto p2_cell = parent->getCellByChild(node2->index);
        int p1_idx = -1, p2_idx = -1;
        for (int i = 0; i < parent->cells.size(); i++) {
            if (p1_cell == parent->cells[i]) p1_idx = i;
            if (p2_cell == parent->cells[i]) p2_idx = i;
        }
        if (p1_idx > p2_idx) {
            std::swap(node1, node2);
        }
    }

    auto parent_element = parent->getCellByChild(node1->index);

    if (node1->size() > node2->size()) {
        // right rotate
        while (node1->size() != target_length && node1->size() > 1) {
            auto moved = node1->cells.end()[-2];
            node1->cells.erase(node1->cells.end() - 2);
            moved->swapChildren(node1->cells.back());
            moved->swapCellValues(parent_element);
            node2->cells.insert(node2->cells.begin(), moved);
        }
    } else if (node1->size() < node2->size()) {
        // left rotate
        while (node1->size() != target_length && node2->size() > 1) {
            auto moved = node2->cells.front();
            node2->cells.erase(node2->cells.begin());

            parent_element->swapCellValues(moved);
            moved->swapChildren(node1->cells.back());

            node1->cells.insert(node1->cells.end() - 1, moved);
        }
    }
    for (auto cell : node1->cells) {
        if (cell->child != MAX_SIZE) {
            btree->get(cell->child)->parent = node1->index;
            cacheUpdate(node1);
        }
    }
    for (auto cell : node2->cells) {
        if (cell->child != MAX_SIZE) {
            btree->get(cell->child)->parent = node2->index;
            cacheUpdate(node2);
        }
    }
}

void BTree::splitNode(std::shared_ptr<BTreeNode> node) {
    cacheUpdate(node);

    auto newnode = newNode();
    newnode->leaf = node->leaf;

    int target_length = node->size() / 2;
    auto center_cell = node->cells[target_length];
    node->cells.erase(node->cells.begin() + target_length);

    auto new_node_last_cell = newnode->cells.front();
    newnode->cells.erase(newnode->cells.begin());

    new_node_last_cell->swapChildren(center_cell);
    if (new_node_last_cell->child != MAX_SIZE) {
        auto swapped_node = btree->get(new_node_last_cell->child);
        swapped_node->parent = newnode->index;
    }

    while (node->size() > target_length) {
        newnode->cells.push_back(node->cells.front());
        node->cells.erase(node->cells.begin());
        if (newnode->cells.back()->child != MAX_SIZE) {
            auto swapped_node = btree->get(newnode->cells.back()->child);
            swapped_node->parent = newnode->index;
        }
    }

    newnode->cells.push_back(new_node_last_cell);
    center_cell->child = newnode->index;

    auto newnode_idx = newnode->index;

    if (node->index == metadata[3]) { // root
        node = btree->get(node->index);
        auto newroot = newNode();

        newroot->cells[0]->child = node->index;
        newroot->cells.insert(newroot->cells.begin(), center_cell);
        newroot->leaf = false;

        node->parent = newroot->index;
        btree->get(newnode_idx)->parent = newroot->index;

        metadata[3] = newroot->index; // root
        metadata[2]++; // height
    } else {
        auto parent_node = btree->get(node->parent);
        btree->get(newnode_idx)->parent = parent_node->index;
        insertCellIntoNode(center_cell, parent_node);
    }
}

bool BTree::rebalanceAfterDeleting(std::shared_ptr<BTreeNode> node) {
    cacheUpdate(node);
    auto siblings = getSiblings(node);
    for (auto idx : siblings) {
        if (idx == MAX_SIZE)
            continue;

        cacheUpdate(node);
        auto node2 = btree->get(idx);
        if (node->size() + node2->size() < BTREE_2D) {
            mergeNodesAfterDeleting(node, node2);
            return true;
        }
    }
    return false;
}

void BTree::mergeNodesAfterDeleting(std::shared_ptr<BTreeNode> node1, std::shared_ptr<BTreeNode> node2) {
    cacheUpdate(node1);
    cacheUpdate(node2);

    if (node1->cells[0]->getKey() > node2->cells[0]->getKey())
        std::swap(node1, node2);

    if (node1->parent == MAX_SIZE || node1->parent != node2->parent)
        throw "Cannot merge root node!";

    auto parentnode = btree->get(node1->parent);
    auto parentcell = parentnode->getCellByChild(node1->index);
    auto ptrcell = parentnode->getCellByChild(node2->index);

    parentcell->swapCellValues(node1->cells.back());
    parentnode->pop(parentcell->getKey());
    ptrcell->child = node1->index;

    for (auto cell : node2->cells) {
        if (cell->child != MAX_SIZE) {
            btree->get(cell->child)->parent = node1->index;
            cacheUpdate(node2);
        }
    }
    cacheUpdate(node1);
    cacheUpdate(node2);

    while (node2->cells.size() > 0) {
        node1->cells.push_back(node2->cells.front());
        node2->cells.erase(node2->cells.begin());
    }

    btree->removeFromCache(node2->index);

    cacheUpdate(parentnode);
    fixNodeAfterDeleting(parentnode);

    if (parentnode->index == metadata[3] && parentnode->size() == 0) {
        metadata[3] = node1->index;
        btree->removeFromCache(parentnode->index);
        metadata[2]--;
        node1->parent = MAX_SIZE;
        node1->leaf = metadata[2] == 1;
    }
}

void BTree::fixNodeAfterDeleting(std::shared_ptr<BTreeNode> node) {
    if (node->index == metadata[3])
        return;
    if (node->size() < BTREE_D) {
        auto status = compensationPhaseOne(node);
        if (!status)
            rebalanceAfterDeleting(node);
    }
}

void BTree::deleteKeyFromNode(int key, std::shared_ptr<BTreeNode> node) {
    cacheUpdate(node);
    if (node->leaf) {
        auto elem = node->pop(key);
        fixNodeAfterDeleting(node);
    } else {
        auto swapped = node->getCellByKey(key);
        auto child = btree->get(swapped->child);
        swapped->swapCellValues(child->cells.end()[-2]);
        deleteKeyFromNode(key, child);
    }
}

std::shared_ptr<Record> BTree::get(int key) {
    auto tup = getNodeForKey(key, false);
    auto node = std::get<0>(tup);
    auto present = std::get<1>(tup);
    if (!present)
        return nullptr;

    auto cell = node->getCellByKey(key);
    if (cell == nullptr)
        return nullptr;

    auto rec = data->get(cell->getPage(), cell->getOffset());
    if (rec == nullptr)
        return nullptr;
    rec->index = key;

    return rec;
}

void BTree::set(int key, std::shared_ptr<Record> rec) {
    auto tup = getNodeForKey(key);
    auto node = std::get<0>(tup);
    auto present = std::get<1>(tup);
    if (present) {
        auto cell = node->getCellByKey(key);
        data->set(cell->getPage(), cell->getOffset(), rec);
    } else {
        auto tup2 = data->insert(rec);
        auto page = std::get<0>(tup2);
        auto offset = std::get<1>(tup2);
        insertCellIntoNode(std::make_shared<BTreeNodeCell>(key, page, offset), node);
        metadata[0]++; // elements
    }
    //btree->set(node->index, node);
}

void BTree::del(int key) {
    auto tup = getNodeForKey(key);
    auto node = std::get<0>(tup);
    auto present = std::get<1>(tup);
    if (!present) {
        throw "Element does not exist!";
    }
    deleteKeyFromNode(key, node);
    metadata[0]--;
}
