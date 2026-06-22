#include "index/b_plus_tree.h"
#include <algorithm>
#include <cassert>

namespace minidb {

BPlusTree::BPlusTree(BufferPool* buffer_pool, page_id_t root_page_id)
    : buffer_pool_(buffer_pool), root_page_id_(root_page_id) {}

page_id_t BPlusTree::Create() {
    std::lock_guard<std::mutex> lock(mutex_);

    page_id_t page_id;
    Page* page = buffer_pool_->NewPage(page_id);
    if (!page) return INVALID_PAGE_ID;

    LeafNode leaf(page->GetData());
    leaf.Init(page_id);

    buffer_pool_->UnpinPage(page_id, true);
    root_page_id_ = page_id;
    return page_id;
}

Status BPlusTree::Search(int32_t key, RID& rid) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (root_page_id_ == INVALID_PAGE_ID) {
        return Status::NotFound("B+ tree is empty");
    }

    page_id_t leaf_page_id = FindLeafPage(key);
    Page* page = buffer_pool_->FetchPage(leaf_page_id);
    if (!page) return Status::InternalError("Cannot fetch leaf page");

    LeafNode leaf(page->GetData());
    int pos = leaf.FindKeyPosition(key);

    if (pos < leaf.Header()->num_keys && leaf.KeyAt(pos) == key) {
        rid = leaf.ValueAt(pos);
        buffer_pool_->UnpinPage(leaf_page_id, false);
        return Status::OK();
    }

    buffer_pool_->UnpinPage(leaf_page_id, false);
    return Status::NotFound("Key not found");
}

Status BPlusTree::Insert(int32_t key, const RID& rid) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (root_page_id_ == INVALID_PAGE_ID) {
        // Create root leaf
        page_id_t page_id;
        Page* page = buffer_pool_->NewPage(page_id);
        if (!page) return Status::OutOfMemory("Cannot create root page");

        LeafNode leaf(page->GetData());
        leaf.Init(page_id);
        leaf.SetKeyAt(0, key);
        leaf.SetValueAt(0, rid);
        leaf.Header()->num_keys = 1;

        buffer_pool_->UnpinPage(page_id, true);
        root_page_id_ = page_id;
        return Status::OK();
    }

    page_id_t leaf_page_id = FindLeafPage(key);
    Page* page = buffer_pool_->FetchPage(leaf_page_id);
    if (!page) return Status::InternalError("Cannot fetch leaf page");

    LeafNode leaf(page->GetData());
    int num_keys = leaf.Header()->num_keys;

    // Check for duplicate key
    int pos = leaf.FindKeyPosition(key);
    if (pos < num_keys && leaf.KeyAt(pos) == key) {
        buffer_pool_->UnpinPage(leaf_page_id, false);
        return Status::DuplicateKey("Key already exists");
    }

    if (num_keys < LEAF_MAX_KEYS) {
        // Room in this leaf — insert in sorted order
        for (int i = num_keys; i > pos; i--) {
            leaf.SetKeyAt(i, leaf.KeyAt(i - 1));
            leaf.SetValueAt(i, leaf.ValueAt(i - 1));
        }
        leaf.SetKeyAt(pos, key);
        leaf.SetValueAt(pos, rid);
        leaf.Header()->num_keys++;

        buffer_pool_->UnpinPage(leaf_page_id, true);
    } else {
        // Leaf is full — need to split
        // Temporarily collect all keys + new key
        std::vector<std::pair<int32_t, RID>> entries;
        entries.reserve(num_keys + 1);
        bool inserted = false;
        for (int i = 0; i < num_keys; i++) {
            if (!inserted && key < leaf.KeyAt(i)) {
                entries.push_back({key, rid});
                inserted = true;
            }
            entries.push_back({leaf.KeyAt(i), leaf.ValueAt(i)});
        }
        if (!inserted) {
            entries.push_back({key, rid});
        }

        // Create new leaf
        page_id_t new_page_id;
        Page* new_page = buffer_pool_->NewPage(new_page_id);
        if (!new_page) {
            buffer_pool_->UnpinPage(leaf_page_id, false);
            return Status::OutOfMemory("Cannot create new leaf");
        }

        LeafNode new_leaf(new_page->GetData());
        new_leaf.Init(new_page_id);

        // Split: first half stays, second half goes to new leaf
        int split = (entries.size() + 1) / 2;

        // Reinitialize old leaf
        leaf.Header()->num_keys = 0;
        for (int i = 0; i < split; i++) {
            leaf.SetKeyAt(i, entries[i].first);
            leaf.SetValueAt(i, entries[i].second);
            leaf.Header()->num_keys++;
        }

        for (size_t i = split; i < entries.size(); i++) {
            int idx = i - split;
            new_leaf.SetKeyAt(idx, entries[i].first);
            new_leaf.SetValueAt(idx, entries[i].second);
            new_leaf.Header()->num_keys++;
        }

        // Update linked list
        new_leaf.SetNextLeaf(leaf.GetNextLeaf());
        leaf.SetNextLeaf(new_page_id);

        // Set parent for new leaf
        new_leaf.Header()->parent_page_id = leaf.Header()->parent_page_id;

        int32_t split_key = new_leaf.KeyAt(0);

        buffer_pool_->UnpinPage(leaf_page_id, true);
        buffer_pool_->UnpinPage(new_page_id, true);

        // Insert split key into parent
        InsertIntoParent(leaf_page_id, split_key, new_page_id);
    }

    return Status::OK();
}

Status BPlusTree::Delete(int32_t key) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (root_page_id_ == INVALID_PAGE_ID) {
        return Status::NotFound("Tree is empty");
    }

    page_id_t leaf_page_id = FindLeafPage(key);
    Page* page = buffer_pool_->FetchPage(leaf_page_id);
    if (!page) return Status::InternalError("Cannot fetch leaf");

    LeafNode leaf(page->GetData());
    int pos = leaf.FindKeyPosition(key);

    if (pos >= leaf.Header()->num_keys || leaf.KeyAt(pos) != key) {
        buffer_pool_->UnpinPage(leaf_page_id, false);
        return Status::NotFound("Key not found");
    }

    // Shift entries left to fill the gap
    int num_keys = leaf.Header()->num_keys;
    for (int i = pos; i < num_keys - 1; i++) {
        leaf.SetKeyAt(i, leaf.KeyAt(i + 1));
        leaf.SetValueAt(i, leaf.ValueAt(i + 1));
    }
    leaf.Header()->num_keys--;

    buffer_pool_->UnpinPage(leaf_page_id, true);

    // Note: We don't do merge/redistribute for simplicity.
    // This is acceptable for a teaching database — real systems would handle underflow.
    return Status::OK();
}

std::vector<RID> BPlusTree::RangeScan(int32_t low_key, int32_t high_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RID> results;

    if (root_page_id_ == INVALID_PAGE_ID) return results;

    page_id_t leaf_page_id = FindLeafPage(low_key);

    while (leaf_page_id != INVALID_PAGE_ID) {
        Page* page = buffer_pool_->FetchPage(leaf_page_id);
        if (!page) break;

        LeafNode leaf(page->GetData());
        int num_keys = leaf.Header()->num_keys;

        for (int i = 0; i < num_keys; i++) {
            int32_t k = leaf.KeyAt(i);
            if (k > high_key) {
                buffer_pool_->UnpinPage(leaf_page_id, false);
                return results;
            }
            if (k >= low_key) {
                results.push_back(leaf.ValueAt(i));
            }
        }

        page_id_t next = leaf.GetNextLeaf();
        buffer_pool_->UnpinPage(leaf_page_id, false);
        leaf_page_id = next;
    }

    return results;
}

std::vector<std::pair<int32_t, RID>> BPlusTree::GetAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<int32_t, RID>> results;

    if (root_page_id_ == INVALID_PAGE_ID) return results;

    // Find leftmost leaf
    page_id_t current = root_page_id_;
    while (true) {
        Page* page = buffer_pool_->FetchPage(current);
        if (!page) break;

        auto* header = reinterpret_cast<BNodeHeader*>(page->GetData());
        if (header->type == BNodeType::LEAF) {
            buffer_pool_->UnpinPage(current, false);
            break;
        }

        InternalNode internal(page->GetData());
        page_id_t child = internal.ChildAt(0);
        buffer_pool_->UnpinPage(current, false);
        current = child;
    }

    // Scan all leaves
    while (current != INVALID_PAGE_ID) {
        Page* page = buffer_pool_->FetchPage(current);
        if (!page) break;

        LeafNode leaf(page->GetData());
        for (int i = 0; i < leaf.Header()->num_keys; i++) {
            results.push_back({leaf.KeyAt(i), leaf.ValueAt(i)});
        }

        page_id_t next = leaf.GetNextLeaf();
        buffer_pool_->UnpinPage(current, false);
        current = next;
    }

    return results;
}

page_id_t BPlusTree::FindLeafPage(int32_t key) {
    page_id_t current = root_page_id_;

    while (current != INVALID_PAGE_ID) {
        Page* page = buffer_pool_->FetchPage(current);
        if (!page) return INVALID_PAGE_ID;

        auto* header = reinterpret_cast<BNodeHeader*>(page->GetData());
        if (header->type == BNodeType::LEAF) {
            buffer_pool_->UnpinPage(current, false);
            return current;
        }

        InternalNode internal(page->GetData());
        int child_idx = internal.FindChildIndex(key);
        page_id_t child = internal.ChildAt(child_idx);
        buffer_pool_->UnpinPage(current, false);
        current = child;
    }

    return INVALID_PAGE_ID;
}

void BPlusTree::InsertIntoParent(page_id_t left_page_id, int32_t key, page_id_t right_page_id) {
    // Get parent of left page
    Page* left_page = buffer_pool_->FetchPage(left_page_id);
    if (!left_page) return;

    auto* left_header = reinterpret_cast<BNodeHeader*>(left_page->GetData());
    page_id_t parent_page_id = left_header->parent_page_id;
    buffer_pool_->UnpinPage(left_page_id, false);

    if (parent_page_id == INVALID_PAGE_ID) {
        // Left was root — create new root
        page_id_t new_root_id;
        Page* new_root_page = buffer_pool_->NewPage(new_root_id);
        if (!new_root_page) return;

        InternalNode new_root(new_root_page->GetData());
        new_root.Init(new_root_id);
        new_root.SetKeyAt(0, key);
        new_root.SetChildAt(0, left_page_id);
        new_root.SetChildAt(1, right_page_id);
        new_root.Header()->num_keys = 1;

        buffer_pool_->UnpinPage(new_root_id, true);

        // Update children's parent pointers
        Page* lp = buffer_pool_->FetchPage(left_page_id);
        if (lp) {
            reinterpret_cast<BNodeHeader*>(lp->GetData())->parent_page_id = new_root_id;
            buffer_pool_->UnpinPage(left_page_id, true);
        }
        Page* rp = buffer_pool_->FetchPage(right_page_id);
        if (rp) {
            reinterpret_cast<BNodeHeader*>(rp->GetData())->parent_page_id = new_root_id;
            buffer_pool_->UnpinPage(right_page_id, true);
        }

        root_page_id_ = new_root_id;
        return;
    }

    // Insert into existing parent
    Page* parent_page = buffer_pool_->FetchPage(parent_page_id);
    if (!parent_page) return;

    InternalNode parent(parent_page->GetData());
    int num_keys = parent.Header()->num_keys;

    if (num_keys < INTERNAL_MAX_KEYS) {
        // Room in parent — find position and insert
        int pos = 0;
        while (pos < num_keys && parent.KeyAt(pos) < key) pos++;

        // Shift keys and children right
        for (int i = num_keys; i > pos; i--) {
            parent.SetKeyAt(i, parent.KeyAt(i - 1));
            parent.SetChildAt(i + 1, parent.ChildAt(i));
        }
        parent.SetKeyAt(pos, key);
        parent.SetChildAt(pos + 1, right_page_id);
        parent.Header()->num_keys++;

        buffer_pool_->UnpinPage(parent_page_id, true);

        // Update right child's parent pointer
        Page* rp = buffer_pool_->FetchPage(right_page_id);
        if (rp) {
            reinterpret_cast<BNodeHeader*>(rp->GetData())->parent_page_id = parent_page_id;
            buffer_pool_->UnpinPage(right_page_id, true);
        }
    } else {
        // Parent is full — split internal node
        // Collect all keys + new key
        std::vector<int32_t> all_keys;
        std::vector<page_id_t> all_children;
        all_children.push_back(parent.ChildAt(0));

        bool inserted = false;
        for (int i = 0; i < num_keys; i++) {
            if (!inserted && key < parent.KeyAt(i)) {
                all_keys.push_back(key);
                all_children.push_back(right_page_id);
                inserted = true;
            }
            all_keys.push_back(parent.KeyAt(i));
            all_children.push_back(parent.ChildAt(i + 1));
        }
        if (!inserted) {
            all_keys.push_back(key);
            all_children.push_back(right_page_id);
        }

        int total = all_keys.size();
        int split = total / 2;
        int32_t push_up_key = all_keys[split];

        // Rewrite old parent with left half
        parent.Header()->num_keys = 0;
        parent.SetChildAt(0, all_children[0]);
        for (int i = 0; i < split; i++) {
            parent.SetKeyAt(i, all_keys[i]);
            parent.SetChildAt(i + 1, all_children[i + 1]);
            parent.Header()->num_keys++;
        }

        // Create new internal node with right half
        page_id_t new_internal_id;
        Page* new_internal_page = buffer_pool_->NewPage(new_internal_id);
        if (!new_internal_page) {
            buffer_pool_->UnpinPage(parent_page_id, true);
            return;
        }

        InternalNode new_internal(new_internal_page->GetData());
        new_internal.Init(new_internal_id);
        new_internal.Header()->parent_page_id = parent.Header()->parent_page_id;
        new_internal.SetChildAt(0, all_children[split + 1]);
        for (int i = split + 1; i < total; i++) {
            int idx = i - split - 1;
            new_internal.SetKeyAt(idx, all_keys[i]);
            new_internal.SetChildAt(idx + 1, all_children[i + 1]);
            new_internal.Header()->num_keys++;
        }

        // Keep child IDs before unpinning; the frame may be evicted afterwards.
        std::vector<page_id_t> moved_children(all_children.begin() + split + 1,
                                               all_children.end());

        buffer_pool_->UnpinPage(parent_page_id, true);
        buffer_pool_->UnpinPage(new_internal_id, true);

        // Update children's parent pointers for the new internal node
        for (page_id_t child_id : moved_children) {
            Page* cp = buffer_pool_->FetchPage(child_id);
            if (cp) {
                reinterpret_cast<BNodeHeader*>(cp->GetData())->parent_page_id = new_internal_id;
                buffer_pool_->UnpinPage(child_id, true);
            }
        }

        // Update right child's parent
        Page* rp = buffer_pool_->FetchPage(right_page_id);
        if (rp) {
            reinterpret_cast<BNodeHeader*>(rp->GetData())->parent_page_id =
                (key <= push_up_key) ? parent_page_id : new_internal_id;
            buffer_pool_->UnpinPage(right_page_id, true);
        }

        // Push up the split key
        InsertIntoParent(parent_page_id, push_up_key, new_internal_id);
    }
}

}  // namespace minidb
