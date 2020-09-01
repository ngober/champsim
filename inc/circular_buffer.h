#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <array>
#include <cassert>
#include <iterator>

template<typename T, typename T_nonconst = T, typename elem_type = typename T::value_type>
class circular_buffer_iterator
{
    private:
        using cbuf_type = T;
        using self_type = circular_buffer_iterator<cbuf_type>;

    public:
        using difference_type   = typename cbuf_type::difference_type;
        using value_type        = typename cbuf_type::value_type;
        using pointer           = typename cbuf_type::pointer;
        using const_pointer     = typename cbuf_type::const_pointer;
        using reference         = typename cbuf_type::reference;
        using const_reference   = typename cbuf_type::const_reference;
        using iterator_category = std::random_access_iterator_tag;

    private:
        cbuf_type*                    buf;
        typename cbuf_type::size_type pos;

    public:
        circular_buffer_iterator(cbuf_type *buf, typename cbuf_type::size_type pos) : buf(buf), pos(pos) {}
        circular_buffer_iterator(const circular_buffer_iterator<T_nonconst, T_nonconst, typename T_nonconst::value_type> &other) : buf(other.buf), pos(other.pos) {}

        friend class circular_buffer_iterator<const T, T, const elem_type>;

        elem_type& operator*()  const { return (*buf)[pos]; }
        elem_type* operator->() const { return &(operator*()); }

        self_type& operator+=(difference_type n) { pos += n; if (pos >= buf->entry_.size()) pos -= buf->entry_.size(); return *this; }
        self_type  operator+(difference_type n)  { self_type r(*this); r += n; return r; }
        self_type& operator-=(difference_type n) { if (pos < n) pos += buf->entry_.size(); pos -= n; return *this; }
        self_type  operator-(difference_type n)  { self_type r(*this); r -= n; return r; }

        self_type& operator++()    { return operator+=(1); }
        self_type  operator++(int) { self_type r(*this); operator++(); return r; }
        self_type& operator--()    { return operator-=(1); }
        self_type  operator--(int) { self_type r(*this); operator--(); return r; }

        bool operator==(const self_type& other) const { return buf == other.buf && pos == other.pos; }
        bool operator!=(const self_type& other) const { return !operator==(other); }
};

/***
 * This class implements a deque-like interface with fixed (maximum) size over contiguous memory.
 * Iterators to this structure are never invalidated, unless the element it refers to is popped.
 */
template<typename T, std::size_t N>
class circular_buffer
{
    private:
        // N+1 elements are used to avoid the aliasing of the full and the empty cases.
        using buffer_t = std::array<T,N+1>;
        using self_type = circular_buffer<T,N>;

    public:
        using value_type             = typename buffer_t::value_type;
        using size_type              = typename buffer_t::size_type;
        using difference_type        = typename buffer_t::difference_type;
        using reference              = value_type&;
        using const_reference        = const value_type&;
        using pointer                = value_type*;
        using const_pointer          = const value_type*;
        using iterator               = circular_buffer_iterator<self_type>;
        using const_iterator         = circular_buffer_iterator<const self_type, self_type, const typename self_type::value_type>;
        using reverse_iterator       = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    private:
        friend iterator;
        friend const_iterator;
        friend reverse_iterator;
        friend const_reverse_iterator;

        buffer_t  entry_     = {};
        size_type head_      = 0;
        size_type tail_      = 0;
        size_type occupancy_ = 0;

        reference operator[](size_type n)             { return entry_[n]; }
        const_reference operator[](size_type n) const { return entry_[n]; }

    public:
        constexpr size_type size() const noexcept     { return N; }
        size_type occupancy() const noexcept          { return occupancy_; };
        bool empty() const noexcept                   { return occupancy() == 0; }
        bool full()  const noexcept                   { return occupancy() == size(); }
        constexpr size_type max_size() const noexcept { return entry_.max_size(); }

        reference front()             { return this->operator[](head_); }
        reference back()              { return this->operator[](tail_ > 0 ? tail_-1 : entry_.size()-1); }
        const_reference front() const { return this->operator[](head_); }
        const_reference back() const  { return this->operator[](tail_ > 0 ? tail_-1 : entry_.size()-1); }

        iterator begin() noexcept              { return iterator(this, head_); }
        iterator end() noexcept                { return iterator(this, tail_); }
        const_iterator begin() const noexcept  { return const_iterator(this, head_); }
        const_iterator end() const noexcept    { return const_iterator(this, tail_); }
        const_iterator cbegin() const noexcept { return const_iterator(this, head_); }
        const_iterator cend() const noexcept   { return const_iterator(this, tail_); }

        iterator rbegin() noexcept              { return reverse_iterator(this, head_); }
        iterator rend() noexcept                { return reverse_iterator(this, tail_); }
        const_iterator rbegin() const noexcept  { return const_reverse_iterator(this, head_); }
        const_iterator rend() const noexcept    { return const_reverse_iterator(this, tail_); }
        const_iterator crbegin() const noexcept { return const_reverse_iterator(this, head_); }
        const_iterator crend() const noexcept   { return const_reverse_iterator(this, tail_); }

        void clear() { head_ = tail_ = occupancy_ = 0; }
        void push_back(const_reference item);
        void pop_front();
};

template<typename T, std::size_t N>
void circular_buffer<T,N>::push_back(circular_buffer<T,N>::const_reference item)
{
    assert(!full());
    entry_[tail_] = item;
    ++tail_;
    ++occupancy_;
    if (tail_ == entry_.size()) tail_ = 0;
}

template<typename T, std::size_t N>
void circular_buffer<T,N>::pop_front()
{
    assert(!empty());
    ++head_;
    --occupancy_;
    if (head_ == entry_.size()) head_ = 0;
}

#endif

