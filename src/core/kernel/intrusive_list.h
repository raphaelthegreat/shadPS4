// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <iterator>

#include "common/parent_of_member.h"
#include "core/kernel/queue.h"

namespace Kernel {

namespace impl {
class IntrusiveListImpl;
}

#pragma pack(push, 4)
struct IntrusiveListNode {
public:
    using TailqEntry = TailqEntry<IntrusiveListNode>;

private:
    TailqEntry m_entry;

public:
    explicit IntrusiveListNode() = default;

    [[nodiscard]] constexpr TailqEntry& GetTailqEntry() {
        return m_entry;
    }
    [[nodiscard]] constexpr const TailqEntry& GetTailqEntry() const {
        return m_entry;
    }

    constexpr void SetTailqEntry(const TailqEntry& entry) {
        m_entry = entry;
    }
};
static_assert(sizeof(IntrusiveListNode) == 2 * sizeof(void*));
#pragma pack(pop)

namespace impl {

class IntrusiveListImpl {
private:
    using RootType = TailqHead<IntrusiveListNode>;

private:
    RootType m_root;

public:
    template <bool Const>
    class Iterator;

    using value_type = IntrusiveListNode;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using reference = value_type&;
    using const_reference = const value_type&;
    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    template <bool Const>
    class Iterator {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = typename IntrusiveListImpl::value_type;
        using difference_type = typename IntrusiveListImpl::difference_type;
        using pointer = std::conditional_t<Const, IntrusiveListImpl::const_pointer,
                                           IntrusiveListImpl::pointer>;
        using reference = std::conditional_t<Const, IntrusiveListImpl::const_reference,
                                             IntrusiveListImpl::reference>;

    private:
        pointer m_node;

    public:
        constexpr explicit Iterator(pointer n) : m_node(n) {}

        constexpr bool operator==(const Iterator& rhs) const {
            return m_node == rhs.m_node;
        }

        constexpr pointer operator->() const {
            return m_node;
        }

        constexpr reference operator*() const {
            return *m_node;
        }

        constexpr Iterator& operator++() {
            m_node = GetNext(m_node);
            return *this;
        }

        constexpr Iterator& operator--() {
            m_node = GetPrev(m_node);
            return *this;
        }

        constexpr Iterator operator++(int) {
            const Iterator it{*this};
            ++(*this);
            return it;
        }

        constexpr Iterator operator--(int) {
            const Iterator it{*this};
            --(*this);
            return it;
        }

        constexpr operator Iterator<true>() const {
            return Iterator<true>(m_node);
        }

        constexpr Iterator<false> GetNonConstIterator() const {
            return Iterator<false>(const_cast<IntrusiveListImpl::pointer>(m_node));
        }
    };

public:
    static constexpr IntrusiveListNode* GetNext(IntrusiveListNode* node) {
        return TAILQ_NEXT(node);
    }

    static constexpr IntrusiveListNode* GetPrev(IntrusiveListNode* node) {
        return TAILQ_PREV(node);
    }

    static constexpr IntrusiveListNode const* GetNext(IntrusiveListNode const* node) {
        return static_cast<const IntrusiveListNode*>(
            GetNext(const_cast<IntrusiveListNode*>(node)));
    }

    static constexpr IntrusiveListNode const* GetPrev(
        IntrusiveListNode const* node) {
        return static_cast<const IntrusiveListNode*>(
            GetPrev(const_cast<IntrusiveListNode*>(node)));
    }

public:
    constexpr IntrusiveListImpl() = default;

    // Iterator accessors.
    constexpr iterator begin() {
        return iterator(this->m_root.tqh_first);
    }

    constexpr const_iterator begin() const {
        return const_iterator(this->m_root.tqh_first);
    }

    constexpr iterator end() {
        return iterator(nullptr);
    }

    constexpr const_iterator end() const {
        return const_iterator(nullptr);
    }

    constexpr const_iterator cbegin() const {
        return this->begin();
    }

    constexpr const_iterator cend() const {
        return this->end();
    }

    constexpr iterator iterator_to(reference ref) {
        return iterator(std::addressof(ref));
    }

    constexpr const_iterator iterator_to(const_reference ref) const {
        return const_iterator(std::addressof(ref));
    }

    // Content management.
    constexpr bool empty() const {
        return m_root.IsEmpty();
    }

    constexpr size_type size() const {
        return static_cast<size_type>(std::distance(this->begin(), this->end()));
    }

    constexpr reference back() {
        return *m_root.Last();
    }

    constexpr const_reference back() const {
        return *m_root.Last();
    }

    constexpr reference front() {
        return *m_root.First();
    }

    constexpr const_reference front() const {
        return *m_root.First();
    }

    constexpr void push_back(reference node) {
        TAILQ_INSERT_TAIL(m_root, std::addressof(node));
    }

    constexpr void push_front(reference node) {
        TAILQ_INSERT_HEAD(m_root, std::addressof(node));
    }

    constexpr void pop_back() {
        TAILQ_REMOVE(m_root, m_root.Last());
    }

    constexpr void pop_front() {
        TAILQ_REMOVE(m_root, m_root.First());
    }

    constexpr iterator insert(const_iterator pos, reference node) {
        TAILQ_INSERT_BEFORE(std::addressof(*pos.GetNonConstIterator()), std::addressof(node));
        return iterator(std::addressof(node));
    }

    constexpr iterator erase(iterator it) {
        auto cur = std::addressof(*it);
        auto next = GetNext(cur);
        TAILQ_REMOVE(m_root, cur);
        return iterator(next);
    }

    constexpr void clear() {
        while (!this->empty()) {
            this->pop_front();
        }
    }
};

} // namespace impl

template <class T, class Traits>
class IntrusiveList {
public:
    using ImplType = impl::IntrusiveListImpl;

private:
    ImplType m_impl;

public:
    template <bool Const>
    class Iterator;

    using value_type = T;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    template <bool Const>
    class Iterator {
    public:
        friend class IntrusiveList<T, Traits>;

        using ImplIterator =
            std::conditional_t<Const, ImplType::const_iterator, ImplType::iterator>;

        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = typename IntrusiveList::value_type;
        using difference_type = typename IntrusiveList::difference_type;
        using pointer = std::conditional_t<Const, IntrusiveList::const_pointer,
                                           IntrusiveList::pointer>;
        using reference = std::conditional_t<Const, IntrusiveList::const_reference,
                                             IntrusiveList::reference>;

    private:
        ImplIterator m_impl;

    private:
        constexpr explicit Iterator(ImplIterator it) : m_impl(it) {}

        constexpr explicit Iterator(typename ImplIterator::pointer p) : m_impl(p) {}

        constexpr ImplIterator GetImplIterator() const {
            return m_impl;
        }

    public:
        constexpr bool operator==(const Iterator& rhs) const {
            return m_impl == rhs.m_impl;
        }

        constexpr pointer operator->() const {
            return Traits::GetParent(std::addressof(*m_impl));
        }

        constexpr reference operator*() const {
            return *Traits::GetParent(std::addressof(*m_impl));
        }

        constexpr Iterator& operator++() {
            ++m_impl;
            return *this;
        }

        constexpr Iterator& operator--() {
            --m_impl;
            return *this;
        }

        constexpr Iterator operator++(int) {
            const Iterator it{*this};
            ++m_impl;
            return it;
        }

        constexpr Iterator operator--(int) {
            const Iterator it{*this};
            --m_impl;
            return it;
        }

        constexpr operator Iterator<true>() const {
            return Iterator<true>(m_impl);
        }
    };

private:
    static constexpr IntrusiveListNode& GetNode(reference ref) {
        return Traits::GetNode(ref);
    }

    static constexpr IntrusiveListNode const& GetNode(const_reference ref) {
        return Traits::GetNode(ref);
    }

    static constexpr reference GetParent(IntrusiveListNode& node) {
        return Traits::GetParent(node);
    }

    static constexpr const_reference GetParent(IntrusiveListNode const& node) {
        return Traits::GetParent(node);
    }

public:
    constexpr IntrusiveList() : m_impl() {}

    // Iterator accessors.
    constexpr iterator begin() {
        return iterator(m_impl.begin());
    }

    constexpr const_iterator begin() const {
        return const_iterator(m_impl.begin());
    }

    constexpr iterator end() {
        return iterator(m_impl.end());
    }

    constexpr const_iterator end() const {
        return const_iterator(m_impl.end());
    }

    constexpr const_iterator cbegin() const {
        return this->begin();
    }

    constexpr const_iterator cend() const {
        return this->end();
    }

    constexpr reverse_iterator rbegin() {
        return reverse_iterator(this->end());
    }

    constexpr const_reverse_iterator rbegin() const {
        return const_reverse_iterator(this->end());
    }

    constexpr reverse_iterator rend() {
        return reverse_iterator(this->begin());
    }

    constexpr const_reverse_iterator rend() const {
        return const_reverse_iterator(this->begin());
    }

    constexpr const_reverse_iterator crbegin() const {
        return this->rbegin();
    }

    constexpr const_reverse_iterator crend() const {
        return this->rend();
    }

    constexpr iterator iterator_to(reference v) {
        return iterator(m_impl.iterator_to(GetNode(v)));
    }

    constexpr const_iterator iterator_to(const_reference v) const {
        return const_iterator(m_impl.iterator_to(GetNode(v)));
    }

    // Content management.
    constexpr bool empty() const {
        return m_impl.empty();
    }

    constexpr size_type size() const {
        return m_impl.size();
    }

    constexpr reference back() {
        return GetParent(m_impl.back());
    }

    constexpr const_reference back() const {
        return GetParent(m_impl.back());
    }

    constexpr reference front() {
        return GetParent(m_impl.front());
    }

    constexpr const_reference front() const {
        return GetParent(m_impl.front());
    }

    constexpr void push_back(reference ref) {
        m_impl.push_back(GetNode(ref));
    }

    constexpr void push_front(reference ref) {
        m_impl.push_front(GetNode(ref));
    }

    constexpr void pop_back() {
        m_impl.pop_back();
    }

    constexpr void pop_front() {
        m_impl.pop_front();
    }

    constexpr iterator insert(const_iterator pos, reference ref) {
        return iterator(m_impl.insert(pos.GetImplIterator(), GetNode(ref)));
    }

    constexpr iterator erase(const_iterator pos) {
        return iterator(m_impl.erase(pos.GetImplIterator()));
    }

    constexpr void clear() {
        m_impl.clear();
    }
};

template <auto T, class Derived = Common::impl::GetParentType<T>>
class IntrusiveListMemberTraits;

template <class Parent, IntrusiveListNode Parent::*Member, class Derived>
class IntrusiveListMemberTraits<Member, Derived> {
public:
    using ListType = IntrusiveList<Derived, IntrusiveListMemberTraits>;
    using ListTypeImpl = impl::IntrusiveListImpl;

private:
    static constexpr IntrusiveListNode* GetNode(Derived* parent) {
        return std::addressof(parent->*Member);
    }

    static constexpr IntrusiveListNode const* GetNode(Derived const* parent) {
        return std::addressof(parent->*Member);
    }

    static Derived* GetParent(IntrusiveListNode* node) {
        return Common::GetParentPointer<Member, Derived>(node);
    }

    static Derived const* GetParent(IntrusiveListNode const* node) {
        return Common::GetParentPointer<Member, Derived>(node);
    }
};


template <class Derived>
class IntrusiveListBaseNode : public IntrusiveListNode {};

template <class Derived>
class IntrusiveListBaseTraits {
public:
    using ListType = IntrusiveList<Derived, IntrusiveListBaseTraits>;
    using ListTypeImpl = impl::IntrusiveListImpl;

private:
    friend class impl::IntrusiveListImpl;

    static constexpr IntrusiveListNode* GetNode(Derived* parent) {
        return static_cast<IntrusiveListNode*>(
            static_cast<IntrusiveListBaseNode<Derived>*>(parent));
    }

    static constexpr IntrusiveListNode const* GetNode(Derived const* parent) {
        return static_cast<const IntrusiveListNode*>(
            static_cast<const IntrusiveListBaseNode<Derived>*>(parent));
    }

    static constexpr Derived* GetParent(IntrusiveListNode* node) {
        return static_cast<Derived*>(static_cast<IntrusiveListBaseNode<Derived>*>(node));
    }

    static constexpr Derived const* GetParent(IntrusiveListNode const* node) {
        return static_cast<const Derived*>(
            static_cast<const IntrusiveListBaseNode<Derived>*>(node));
    }
};


} // namespace Kernel
