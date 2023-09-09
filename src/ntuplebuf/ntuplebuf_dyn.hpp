#ifndef ntuplebuf_dyn_hpp
#define ntuplebuf_dyn_hpp

/*
N-tuple buffer with dynamically allocated memory for messages.
The memory is allocated at construction time as zero-initialized byte array.
Every message buffer is aligned as std::max_align_t, so it is suitable for
almost all data types
 */


#include "ntuplebuf.hpp"
#include <cstddef>
#include  <algorithm> // std::min

namespace ntuplebuf {


template<typename ControlCodeT, unsigned NBUFS>  // ToDo:  + panic/warning handler?
struct NTupleBufferDynAlloc
{
    typedef int errcode_t;

    NTupleBufferDynAlloc(size_t data_size){
        size_t algn = alignof(std::max_align_t); // provide maximum alignment
        sz1buf_ = ((data_size + algn -1) / algn) * algn; //size of one buffer
        data_ = new uint8_t[NBUFS * sz1buf_](); // zero-initialized
    }

    ~NTupleBufferDynAlloc(){
        delete[] data_;
    }

    errcode_t start_reading(void** pptr){ // pptr shall point to previous pointer to buffer (or nullptr)
        int bufnum = ptr2bufnum(*pptr);
        auto res = er(control.start_reading(&bufnum));
        if(res >= 0){
            *pptr = bufnum2ptr(bufnum);
        }
        return res;
    }

    errcode_t free(void** pptr){
        int bufnum = ptr2bufnum(*pptr);
        auto res = er(control.free(&bufnum));
        if(res >= 0){
           *pptr = nullptr;
        }
        return res;
    }

    errcode_t consume(void** pptr){
        int bufnum = ptr2bufnum(*pptr);
        auto res = er(control.consume(&bufnum));
        if(res >= 0){
           *pptr = nullptr;
        }
        return res;
    }

    errcode_t start_writing(void** pptr){
        int bufnum = ptr2bufnum(*pptr);
        auto res = er(control.start_writing(&bufnum));
        if(res >= 0){
            *pptr = bufnum2ptr(bufnum);
        }
        return res;
    }

    errcode_t commit(void** pptr){
        int bufnum = ptr2bufnum(*pptr);
        auto res = er(control.commit(&bufnum));
        if(res >= 0){
           *pptr = nullptr;
        }
        return res;
    }

protected:

    errcode_t er(int fr){return std::min(0, fr);}

    int ptr2bufnum(void* ptr){
        uint8_t* p = (uint8_t*)ptr;
        return (p == nullptr)? 0 : ((p - data_) / sz1buf_ + 1);
    }

    void* bufnum2ptr(int bufnum){
        return (bufnum != 0)
            ? data_ + sz1buf_* (bufnum -1)
            : nullptr;
    }

    size_t sz1buf_; // size of 1 buffer
    NTupleBufferControl<ControlCodeT, NBUFS> control;
    uint8_t* data_ = nullptr;
};


template<typename ControlCodeT, unsigned NBUFS, typename DataT>
struct NTupleBufferDynAllocTyped
    : public NTupleBufferDynAlloc<ControlCodeT, NBUFS>
{
    typedef NTupleBufferDynAlloc<ControlCodeT, NBUFS> Base;
    typedef typename Base::errcode_t errcode_t;

    NTupleBufferDynAllocTyped(){
        for(int i=0 ; i < NBUFS; ++i){
            new(Base::data_ + i * Base::sz1buf_) DataT; // call placement new
        }
    }

    ~NTupleBufferDynAllocTyped(){
        for(int i=0 ; i < NBUFS; ++i){
            ((DataT*)(void*)(Base::data_ + i * Base::sz1buf_)) -> DataT::~DataT(); // placement destruct
        }
    }

    errcode_t start_reading(DataT** pptr){ // pptr shall point to previous pointer to buffer (or nullptr)
        return Base::start_reading((void**)pptr);
    }

    errcode_t free(DataT** pptr){
        return Base::free((void**)pptr);
    }

    errcode_t consume(DataT** pptr){
        return Base::consume((void**)pptr);
    }

    errcode_t start_writing(DataT** pptr){
        auto res = Base::start_writing((void**)pptr);
        if(res > 0){
            (*pptr) -> DataT::~DataT(); // destruct previous data in just allocated buffer
            new(*pptr) DataT; // (placement) construct new data
        }
        return res;
    }

    errcode_t commit(DataT** pptr){
        return Base::commit((void**)pptr);
    }
};

} // namespace

#endif
