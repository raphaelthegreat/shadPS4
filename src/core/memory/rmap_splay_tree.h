// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Core {

struct RmapNode {
    RmapNode* parent{};
    RmapNode* left{};
    RmapNode* right{};
    RmapNode* prev{};
    RmapNode* next{};
    RmapNode* _next{};
    VAddr vaddr{};
    s32 p_start_idx{};
    s32 p_end_idx{};
    s32 p_adj_free_idx{};
};

template <typename Traits>
class RmapSplayTree {
public:
    using Entry = typename Traits::EntryType;

private:
    using N = RmapNode;

    static N* GetNode(Entry* entry) {
        return Traits::GetNode(entry);
    }
    static Entry* GetEntry(N* node) {
        return Traits::FromNode(node);
    }

    class Iterator {
    public:
        Iterator(N* node) : m_node{node} {}

        Entry& operator*() const {
            return *GetEntry(m_node);
        }
        Entry* operator->() const {
            return GetEntry(m_node);
        }
        Iterator& operator++() {
            m_node = m_node->next; return *this;
        }
        bool operator!=(const Iterator& o) const {
            return m_node != o.m_node;
        }
    private:
        N* m_node;
    };

    static int CompareKeys(s32 a_idx, VAddr a_va, s32 b_idx, VAddr b_va) {
        if (a_idx < b_idx) {
            return -1;
        }
        if (a_idx > b_idx) {
            return 1;
        }
        if (static_cast<s64>(a_va) < static_cast<s64>(b_va)) {
            return -1;
        }
        if (static_cast<s64>(a_va) > static_cast<s64>(b_va)) {
            return 1;
        }
        return 0;
    }

    static int CompareToNode(s32 idx, VAddr va, N* node) {
        return CompareKeys(idx, va, node->p_start_idx, node->vaddr);
    }

    void UpdateAug(N* node) {
        s32 val = node->p_end_idx;
        if (node->left != &m_nil && node->left->p_adj_free_idx > val) {
            val = node->left->p_adj_free_idx;
        }
        if (node->right != &m_nil && node->right->p_adj_free_idx > val) {
            val = node->right->p_adj_free_idx;
        }
        node->p_adj_free_idx = val;
    }

    void SplayInternal(N* root, N* node) {
        if (root == node) {
            return;
        }

        N* parent = node->parent;
        if (parent != root) {
            // Zig-zig / zig-zag loop
            N* left_child = parent->left;
            N* ggp;
            do {
                N* gp = parent->parent;
                ggp = gp->parent;
                N* rotated = node;

                if (left_child == node) {
                    // Node is left child of parent
                    N* gp_left = gp->left;
                    parent->left = node->right;
                    node->right->parent = parent;
                    node->right = parent;
                    parent->parent = node;

                    N** gp_slot;
                    N** node_slot;
                    if (gp_left == parent) {
                        // Zig-zig: both left
                        gp_slot = &gp->left;
                        node_slot = &parent->right;
                        rotated = parent;
                    } else {
                        // Zig-zag: parent is right child of gp
                        gp_slot = &gp->right;
                        node_slot = &node->left;
                    }
                    *gp_slot = *node_slot;
                    (*node_slot)->parent = gp;
                    *node_slot = gp;
                    gp->parent = rotated;
                } else {
                    // Node is right child of parent
                    N* gp_right = gp->right;
                    parent->right = node->left;
                    node->left->parent = parent;
                    node->left = parent;
                    parent->parent = node;

                    N** gp_slot;
                    N** node_slot;
                    if (gp_right == parent) {
                        // Zig-zig: both right
                        gp_slot = &gp->right;
                        node_slot = &parent->left;
                        rotated = parent;
                    } else {
                        // Zig-zag: parent is left child of gp
                        gp_slot = &gp->left;
                        node_slot = &node->right;
                    }
                    *gp_slot = *node_slot;
                    (*node_slot)->parent = gp;
                    *node_slot = gp;
                    gp->parent = rotated;
                }

                // Propagate augmentation bottom-up
                node->p_adj_free_idx = gp->p_adj_free_idx;
                UpdateAug(gp);
                UpdateAug(parent);
                node->parent = ggp;

                if (gp == root) {
                    return;
                }

                // Prepare for next iteration
                N* ggp_left = ggp->left;
                left_child = ggp_left;
                if (ggp_left == gp) {
                    left_child = node;
                    ggp->left = node;
                } else {
                    ggp->right = node;
                }
                parent = ggp;
            } while (ggp != root);
        }

               // Zig: single rotation
        if (root->left == node) {
            N* nr = node->right;
            root->left = nr;
            nr->parent = root;
            node->right = root;
        } else {
            N* nl = node->left;
            root->right = nl;
            nl->parent = root;
            node->left = root;
        }
        node->parent = root->parent;
        root->parent = node;
        node->p_adj_free_idx = root->p_adj_free_idx;
        UpdateAug(root);
    }

    void SplayToRoot(N* node) {
        SplayInternal(m_root, node);
        m_root = node;
    }

public:
    RmapSplayTree() {
        Init();
    }

    void Init() {
        // nil sentinel: all fields zero, self-referential where needed
        m_nil.parent = &m_nil;
        m_nil.left = &m_nil;
        m_nil.right = &m_nil;
        m_nil.prev = &m_nil;
        m_nil.next = &m_nil;
        m_nil.p_adj_free_idx = 0;

        // head sentinel (list start)
        m_head.parent = &m_nil;
        m_head.left = &m_nil;
        m_head.right = &m_tail;
        m_head.next = &m_tail;
        m_head.prev = &m_tail;
        m_head.p_start_idx = 0;
        m_head.p_end_idx = 0;
        m_head.p_adj_free_idx = 0x7fffffff;

        // tail sentinel (list end)
        m_tail.parent = &m_head;
        m_tail.left = &m_nil;
        m_tail.right = &m_nil;
        m_tail.next = &m_head;
        m_tail.prev = &m_head;
        m_tail.p_start_idx = 0x7fffffff;
        m_tail.p_end_idx = 0x7fffffff;
        m_tail.p_adj_free_idx = 0x7fffffff;

        m_root = &m_head;
    }

    Iterator begin() {
        return Iterator(m_head.next);
    }
    Iterator end() {
        return Iterator(&m_tail);
    }

    bool IsEmpty() const {
        return m_head.next == &m_tail;
    }

    N* Nil() {
        return &m_nil;
    }
    N* Head() {
        return &m_head;
    }
    N* Tail() {
        return &m_tail;
    }

    Entry* Find(s32 page_idx, VAddr vaddr) {
        N* cur = m_root;
        N* result = cur;
        while (true) {
            result = cur;
            int cmp = CompareToNode(page_idx, vaddr, cur);
            if (cmp < 0) {
                cur = cur->left;
            } else if (cmp > 0) {
                cur = cur->right;
            } else {
                return GetEntry(result);
            }
            if (cur == &m_nil) {
                return GetEntry(result);
            }
        }
    }

    void Splay(Entry* entry) {
        SplayToRoot(GetNode(entry));
    }

    void Insert(Entry* entry) {
        N* node = GetNode(entry);
        N* root = m_root;

        int cmp = CompareKeys(root->p_start_idx, root->vaddr,
                              node->p_start_idx, node->vaddr);

        if (cmp < 0) {
            // Root key < new key → new goes to the right of root.
            // New gets root's right subtree; root's right becomes nil.
            node->next = root->next;
            root->next->prev = node;
            node->prev = root;
            root->next = node;
            node->right = root->right;
            root->right->parent = node;
            root->right = &m_nil;

            s32 aug = root->left->p_adj_free_idx;
            if (aug < root->p_end_idx) {
                aug = root->p_end_idx;
            }
            root->p_adj_free_idx = aug;
            node->left = root;
        } else {
            // Root key >= new key → new goes to the left of root.
            // New gets root's left subtree; root's left becomes nil.
            node->prev = root->prev;
            root->prev->next = node;
            node->next = root;
            root->prev = node;
            node->left = root->left;
            root->left->parent = node;
            root->left = &m_nil;

            s32 aug = root->right->p_adj_free_idx;
            if (aug < root->p_end_idx) {
                aug = root->p_end_idx;
            }
            root->p_adj_free_idx = aug;
            node->right = root;
        }

        root->parent = node;
        s32 aug = root->p_adj_free_idx;
        if (aug < node->p_end_idx) {
            aug = node->p_end_idx;
        }
        node->p_adj_free_idx = aug;
        node->parent = &m_nil;
        m_root = node;
    }

    void Remove(Entry* entry) {
        N* node = GetNode(entry);
        SplayToRoot(node);

        // Unlink from list
        node->prev->next = node->next;
        node->next->prev = node->prev;

        if (node->left == &m_nil) {
            m_root = node->right;
            m_root->parent = &m_nil;
        } else if (node->right == &m_nil) {
            m_root = node->left;
            m_root->parent = &m_nil;
        } else {
            // Splay the successor (next in list) within the right subtree,
            // then attach left subtree.
            N* successor = node->next;
            SplayInternal(node->right, successor);
            successor->left = node->left;
            node->left->parent = successor;
            successor->parent = &m_nil;
            UpdateAug(successor);
            m_root = successor;
        }

        // Clear removed node
        node->left = nullptr;
        node->right = nullptr;
        node->parent = nullptr;
        node->prev = nullptr;
        node->next = nullptr;
    }

    /// Find all entries whose physical page range overlaps with [start_page, end_page).
    /// Returns a singly-linked list via the _next pointer.
    Entry* FindOverlapping(s32 start_page, s32 end_page) {
        if (IsEmpty()) {
            return nullptr;
        }

        // First, splay around end_page to get a good starting point.
        Entry* nearest = Find(end_page, static_cast<VAddr>(0x8000000000000000ULL));
        SplayToRoot(GetNode(nearest));

        N* root = m_root;
        N* hit_list = nullptr;

        // Check if root overlaps.
        if (root->p_start_idx < end_page && start_page < root->p_end_idx) {
            root->_next = nullptr;
            hit_list = root;
        }

        // Search the left subtree using p_adj_free_idx for pruning.
        N* cursor = root->left;
        if (cursor != &m_nil && start_page < cursor->p_adj_free_idx) {
            N* stack = nullptr;

            while (true) {
                // Check current node for overlap
                if (cursor->p_start_idx < end_page &&
                    start_page < cursor->p_end_idx) {
                    cursor->_next = hit_list;
                    hit_list = cursor;
                }

                // Try to descend left (if subtree might contain overlaps)
                N* right_child = cursor->right;
                if (cursor->left != &m_nil &&
                    cursor->left->p_adj_free_idx > start_page) {
                    // Save right child on stack if it might have overlaps
                    if (right_child != &m_nil) {
                        right_child->_next = stack;
                        stack = right_child;
                    }
                    cursor = cursor->left;
                } else if (right_child != &m_nil && start_page < right_child->p_adj_free_idx) {
                    cursor = right_child;
                } else {
                    // Pop from stack
                    if (!stack) {
                        break;
                    }
                    cursor = stack;
                    stack = stack->_next;
                    cursor->_next = nullptr;
                }
            }
        }

        return hit_list ? GetEntry(hit_list) : nullptr;
    }

    Entry* First() {
        N* n = m_head.next;
        return (n == &m_tail) ? nullptr : GetEntry(n);
    }

    Entry* Last() {
        N* n = m_tail.prev;
        return (n == &m_head) ? nullptr : GetEntry(n);
    }

    Entry* Next(Entry* e) {
        N* n = GetNode(e)->next;
        return (n == &m_tail) ? nullptr : GetEntry(n);
    }

    Entry* Prev(Entry* e) {
        N* n = GetNode(e)->prev;
        return (n == &m_head) ? nullptr : GetEntry(n);
    }

    static Entry* NextOverlap(Entry* e) {
        N* n = GetNode(e)->_next;
        return n ? GetEntry(n) : nullptr;
    }

private:
    N m_nil{};
    N m_head{};
    N m_tail{};
    N* m_root{};
};

template <typename T>
struct InheritRmapTraits {
    using EntryType = T;

    static RmapNode* GetNode(T* entry) {
        return static_cast<RmapNode*>(entry);
    }
    static T* FromNode(RmapNode* node) {
        return static_cast<T*>(node);
    }
};

} // namespace Core