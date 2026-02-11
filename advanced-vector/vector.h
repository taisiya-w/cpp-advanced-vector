#pragma once

#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <algorithm>
#include <memory>
#include <type_traits>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory&) = delete;

    RawMemory(RawMemory&& other) noexcept {
        Swap(other);
    }

    RawMemory& operator=(RawMemory&& other) noexcept {
        if (this != &other) {
            Swap(other);
        }
        return *this;
    }

    T* operator+(size_t offset) noexcept {
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() noexcept {
        return buffer_.GetAddress();
    }

    iterator end() noexcept {
        return buffer_.GetAddress() + size_;
    }

    const_iterator begin() const noexcept {
        return buffer_.GetAddress();
    }

    const_iterator end() const noexcept {
        return buffer_.GetAddress() + size_;
    }

    const_iterator cbegin() const noexcept {
        return begin();
    }

    const_iterator cend() const noexcept {
        return end();
    }

    Vector() noexcept = default;

    explicit Vector(size_t size)
        : buffer_(size)
        , size_(size) 
    {
        std::uninitialized_value_construct_n(buffer_.GetAddress(), size_);
    }

    Vector(const Vector& other)
        : buffer_(other.size_)
        , size_(other.size_)
    {
        std::uninitialized_copy_n(other.buffer_.GetAddress(), size_, buffer_.GetAddress());
    }

    Vector(Vector&& other) noexcept {
        Swap(other);
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > buffer_.Capacity()) {
                Vector tmp(rhs);
                Swap(tmp);
            } else {
                AssignFromVector(rhs);
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    ~Vector() {
        std::destroy_n(buffer_.GetAddress(), size_);
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= buffer_.Capacity()) {
            return;
        }
        
        RawMemory<T> new_buffer(new_capacity);
        
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(buffer_.GetAddress(), size_, new_buffer.GetAddress());
        } else {
            std::uninitialized_copy_n(buffer_.GetAddress(), size_, new_buffer.GetAddress());
        }
        
        std::destroy_n(buffer_.GetAddress(), size_);
        buffer_.Swap(new_buffer);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return buffer_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return buffer_[index];
    }

    void Swap(Vector& other) noexcept {
        buffer_.Swap(other.buffer_);
        std::swap(size_, other.size_);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(buffer_.GetAddress() + new_size, size_ - new_size);
            size_ = new_size;
        } else if (new_size > size_) {
            Reserve(new_size);
            std::uninitialized_value_construct_n(buffer_.GetAddress() + size_, new_size - size_);
            size_ = new_size;
        }
    }

    void PushBack(const T& value) {
        EmplaceBackInternal(value);
    }

    void PushBack(T&& value) {
        EmplaceBackInternal(std::move(value));
    }

    void PopBack() noexcept {
        assert(size_ > 0 && "PopBack on empty vector");
        std::destroy_at(buffer_.GetAddress() + size_ - 1);
        --size_;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (size_ == Capacity()) {
            size_t new_capacity = size_ == 0 ? 1 : size_ * 2;
            RawMemory<T> new_buffer(new_capacity);
            
            T* new_elem_ptr = new_buffer + size_;
            new (new_elem_ptr) T(std::forward<Args>(args)...);
            
            try {
                if constexpr (std::is_nothrow_move_constructible_v<T> ||
                              !std::is_copy_constructible_v<T>) {
                    std::uninitialized_move_n(buffer_.GetAddress(), size_,
                                              new_buffer.GetAddress());
                } else {
                    std::uninitialized_copy_n(buffer_.GetAddress(), size_,
                                              new_buffer.GetAddress());
                }
            } catch (...) {
                std::destroy_at(new_elem_ptr);
                throw;
            }
            
            std::destroy_n(buffer_.GetAddress(), size_);
            buffer_.Swap(new_buffer);
            ++size_;
            return *new_elem_ptr;
        } else {
            T* ptr = buffer_ + size_;
            new (ptr) T(std::forward<Args>(args)...);
            ++size_;
            return *ptr;
        }
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        assert(pos >= cbegin() && pos <= cend() && "Iterator out of range");
        
        size_t index = pos - cbegin();

        if (index == size_) {
            EmplaceBack(std::forward<Args>(args)...);
            return begin() + index;
        }

        if (size_ == Capacity()) {
            return EmplaceWithReallocation(index, std::forward<Args>(args)...);
        }

        return EmplaceInPlace(index, std::forward<Args>(args)...);
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
        assert(pos >= cbegin() && pos < cend() && "Iterator out of range");
        
        size_t index = pos - cbegin();
        
        std::move(begin() + index + 1, end(), begin() + index);
        std::destroy_at(buffer_.GetAddress() + size_ - 1);
        --size_;
        return begin() + index;
    }

private:
    template <typename U>
    void EmplaceBackInternal(U&& value) {
        if (size_ == Capacity()) {
            size_t new_capacity = size_ == 0 ? 1 : size_ * 2;
            RawMemory<T> new_buffer(new_capacity);
            
            new (new_buffer + size_) T(std::forward<U>(value));
            
            try {
                if constexpr (std::is_nothrow_move_constructible_v<T> || 
                            !std::is_copy_constructible_v<T>) {
                    std::uninitialized_move_n(buffer_.GetAddress(), size_, 
                                            new_buffer.GetAddress());
                } else {
                    std::uninitialized_copy_n(buffer_.GetAddress(), size_, 
                                            new_buffer.GetAddress());
                }
            } catch (...) {
                std::destroy_at(new_buffer + size_);
                throw;
            }
            
            std::destroy_n(buffer_.GetAddress(), size_);
            buffer_.Swap(new_buffer);
        } else {
            new (buffer_ + size_) T(std::forward<U>(value));
        }
        ++size_;
    }

    void AssignFromVector(const Vector& rhs) {
        size_t min_size = std::min(size_, rhs.size_);
        
        std::copy(rhs.buffer_.GetAddress(), 
                  rhs.buffer_.GetAddress() + min_size, 
                  buffer_.GetAddress());
        
        if (rhs.size_ > size_) {
            std::uninitialized_copy_n(
                rhs.buffer_.GetAddress() + size_, 
                rhs.size_ - size_, 
                buffer_.GetAddress() + size_
            );
        } else if (size_ > rhs.size_) {
            std::destroy_n(
                buffer_.GetAddress() + rhs.size_, 
                size_ - rhs.size_
            );
        }
        
        size_ = rhs.size_;
    }

    template <typename... Args>
    iterator EmplaceWithReallocation(size_t index, Args&&... args) {
        size_t new_capacity = size_ == 0 ? 1 : size_ * 2;
        RawMemory<T> new_buffer(new_capacity);
        T* new_buf = new_buffer.GetAddress();
        T* old_buf = buffer_.GetAddress();

        new (new_buf + index) T(std::forward<Args>(args)...);

        try {
            if constexpr (std::is_nothrow_move_constructible_v<T> ||
                        !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(old_buf, index, new_buf);
            } else {
                std::uninitialized_copy_n(old_buf, index, new_buf);
            }
        } catch (...) {
            std::destroy_at(new_buf + index);
            throw;
        }

        try {
            if constexpr (std::is_nothrow_move_constructible_v<T> ||
                        !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(old_buf + index, size_ - index,
                                        new_buf + index + 1);
            } else {
                std::uninitialized_copy_n(old_buf + index, size_ - index,
                                        new_buf + index + 1);
            }
        } catch (...) {
            std::destroy_n(new_buf, index);
            std::destroy_at(new_buf + index);
            throw;
        }

        std::destroy_n(old_buf, size_);
        buffer_.Swap(new_buffer);
        ++size_;
        return begin() + index;
    }

    template <typename... Args>
    iterator EmplaceInPlace(size_t index, Args&&... args) {
        T tmp(std::forward<Args>(args)...);

        new (buffer_ + size_) T(std::move(buffer_[size_ - 1]));
        ++size_; 

        try {
            std::move_backward(begin() + index, end() - 2, end() - 1);
        } catch (...) {
            std::destroy_at(buffer_ + size_ - 1);
            --size_;
            throw;
        }

        buffer_[index] = std::move(tmp);
        return begin() + index;
    }

private:
    RawMemory<T> buffer_;
    size_t size_ = 0;
};
