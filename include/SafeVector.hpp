#pragma once

#include <vector>
#include <memory>
#include <functional>

// A vector that can be modified while iterating
template <typename T>
class SafeVector {
public:
    using PointerType = typename std::pointer_traits<T>::element_type*;

    void push_back(T item) {
        data.push_back(std::move(item));
    }

    // Safe to call when vector is empty
    void pop_back() {
        if(!data.empty())
            data.pop_back();
    }

    // Might not remove the data immediately if inside for_each loop.
    // In that case the item is removed at the end of the loop.
    void remove(PointerType item_to_remove) {
        if(for_each_depth == 0)
            remove_item(item_to_remove);
        else
            remove_queue.push_back(item_to_remove);
    }

    // Safe to call when vector is empty, in which case it returns nullptr
    T* back() {
        if(data.empty())
            return nullptr;
        else
            return &data.back();
    }

    void clear() {
        data.clear();
        remove_queue.clear();
    }

    // Return true from |callback| to continue. This function returns false if |callback| returned false
    bool for_each(std::function<bool(T &t)> callback) {
        bool result = true;
        ++for_each_depth;

        for(size_t i = 0; i < data.size(); ++i) {
            result = callback(data[i]);
            if(!result)
                break;
        }

        --for_each_depth;
        if(for_each_depth == 0) {
            for(PointerType item_to_remove : remove_queue) {
                remove_item(item_to_remove);
            }
            remove_queue.clear();
        }

        return result;
    }

    // Return true from |callback| to continue. This function returns false if |callback| returned false
    bool for_each_reverse(std::function<bool(T &t)> callback) {
        bool result = true;
        ++for_each_depth;

        int i = (int)data.size() - 1;
        while(true) {
            // pop_back can be called while iterating so handle that case
            if(i >= (int)data.size())
                i = (int)data.size() - 1;

            if(i < 0)
                break;

            result = callback(data[i]);
            if(!result)
                break;

            --i;
        }

        --for_each_depth;
        if(for_each_depth == 0) {
            for(PointerType item_to_remove : remove_queue) {
                remove_item(item_to_remove);
            }
            remove_queue.clear();
        }

        return result;
    }

    T& operator[](size_t index) {
        return data[index];
    }

    const T& operator[](size_t index) const {
        return data[index];
    }

    size_t size() const {
        return data.size();
    }

    bool empty() const {
        return data.empty();
    }
private:
    void remove_item(PointerType item_to_remove) {
        for(auto it = data.begin(), end = data.end(); it != end; ++it) {
            if(&*(*it) == item_to_remove) {
                data.erase(it);
                return;
            }
        }
    }
private:
    std::vector<T> data;
    std::vector<PointerType> remove_queue;
    int for_each_depth = 0;
};