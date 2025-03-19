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

/**
 * Buffer data as byte array (typeless)
 */
template<typename ControlCodeT, unsigned NBUFS>
struct NTupleBufferDynAlloc
{
    typedef int errcode_t;
    typedef  NTupleBufferControl<ControlCodeT, NBUFS> ControlCode;
    typedef typename ControlCode::Transaction CCTransaction;

    struct TypelessTransacion{
      errcode_t errcode;
      void* old_buf;
      void* new_buf;
    };

    NTupleBufferDynAlloc(size_t data_size)
        : data_size_(data_size) // size of one buffer as passed to ctor
    {
        size_t algn = alignof(std::max_align_t); // provide maximum alignment
        sz1buf_ = ((data_size + algn -1) / algn) * algn; //size of one buffer (as allocated)
        data_ = new uint8_t[NBUFS * sz1buf_](); // zero-initialized
    }

    ~NTupleBufferDynAlloc(){
        delete[] data_;
    }

    size_t get_data_size(){ return data_size_; };

    errcode_t start_reading(void** pptr){ // pptr shall point to previous pointer to buffer (or nullptr)
        int bufnum = ptr2bufnum(*pptr);
        auto res = er(control.start_reading(&bufnum));
        if(res >= 0){
            *pptr = bufnum2ptr(bufnum);
        }
        return res;
    }

    errcode_t pop(void** pptr){ // pptr shall point to previous pointer to buffer (or nullptr)
        int bufnum = ptr2bufnum(*pptr);
        auto res = er(control.pop(&bufnum));
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


    TypelessTransacion start_transaction(){
        CCTransaction tr = control.start_transaction();
        TypelessTransacion ret = {
                tr.errcode,
                bufnum2ptr(tr.old_buf),
                bufnum2ptr(tr.new_buf),
        };

        return ret;
    }

    errcode_t commit_transaction(TypelessTransacion& tra, bool force){
        if(tra.errcode != 0){
            return -91;
        }

        CCTransaction cctra = {
                0,
                ptr2bufnum(tra.old_buf),
                ptr2bufnum(tra.new_buf)
        };

        int res = control.commit_transaction(cctra, force);

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

    size_t data_size_;
    size_t sz1buf_; // size of 1 buffer
    ControlCode control;
    uint8_t* data_ = nullptr;
};


/**
 * Buffer data as type.
 * The type shall be default constructible
 */
template<typename ControlCodeT, unsigned NBUFS, typename DataT>
struct NTupleBufferDynAllocTyped
    : public NTupleBufferDynAlloc<ControlCodeT, NBUFS>
{
    typedef NTupleBufferDynAlloc<ControlCodeT, NBUFS> Base;
    typedef typename Base::errcode_t errcode_t;
    typedef typename Base::TypelessTransacion TypelessTransacion;

    struct TypedTransacion{
      errcode_t errcode;
      DataT* old_buf;
      DataT* new_buf;
    };


    NTupleBufferDynAllocTyped()
    : Base(sizeof(DataT))
    {
        for(unsigned i=0 ; i < NBUFS; ++i){
            new(Base::data_ + i * Base::sz1buf_) DataT; // call placement new
        }
    }

    ~NTupleBufferDynAllocTyped(){
        for(unsigned i=0 ; i < NBUFS; ++i){
            (static_cast<DataT*>(
                    static_cast<void*>(
                            Base::data_ + i * Base::sz1buf_
            ))) -> DataT::~DataT(); // placement destruct
        }
    }

    errcode_t start_reading(DataT** pptr){ // pptr shall point to previous pointer to buffer (or nullptr)
        return Base::start_reading(ppD2V(pptr));
    }

    errcode_t pop(DataT** pptr){ // pptr shall point to previous pointer to buffer (or nullptr)
        return Base::pop(ppD2V(pptr));
    }

    errcode_t free(DataT** pptr){
        return Base::free(ppD2V(pptr));
    }

    errcode_t consume(DataT** pptr){
        return Base::consume(ppD2V(pptr));
    }

    errcode_t start_writing(DataT** pptr){
        auto res = Base::start_writing(ppD2V(pptr));
        if(res > 0){
            reconstruct(*pptr);
        }
        return res;
    }

    errcode_t commit(DataT** pptr){
        return Base::commit(ppD2V(pptr));
    }

    TypedTransacion start_transaction(){
        TypelessTransacion tr = Base::start_transaction();
        TypedTransacion ret = {
                tr.errcode,
                static_cast<DataT*>(tr.old_buf),
                static_cast<DataT*>(tr.new_buf)
        };

        if(tr.errcode == 0){
            // recreate object in newly allocated buffer:
            this->reconstruct(ret.new_buf);
        }

        return ret;
    }

    errcode_t commit_transaction(TypedTransacion& tra, bool force){
        if(tra.errcode != 0){
            return -92;
        }

        TypelessTransacion tr = {
                tra.errcode,
                tra.old_buf,
                tra.new_buf,
        };

        int res = Base::commit_transaction(tr, force);

        return res;
    }


private:
    // the "function" just casts Data** to void** :
    static void** ppD2V(DataT** ppd){return static_cast<void**>(static_cast<void*>(ppd));}

    void reconstruct(DataT* pd){
        pd -> DataT::~DataT(); // destruct previous data
        new(pd) DataT; // (placement) construct new data
    }

};

} // namespace

#endif
