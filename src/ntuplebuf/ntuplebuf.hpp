/*

Atomic control structure of ntuple (e.g. triple) buffer.
The control structure does not contain buffers themselves, it operates in terms of buffer number
and reference counters provided for all buffers.
Reference counters and a "pointer" to (i.e. number of) most recent committed message are packed into
single integral atomic.
 */


#ifndef ntuplebuf_ntuplebuf_h
#define ntuplebuf_ntuplebuf_h

#include <cstddef>
#include <climits>
#include <type_traits>
#include <atomic>


#if TEST_RACES_ntuplebuf_ms
#   include <thread>
#   include <cstdlib>
#   include <chrono>
#   define  YELD_ntuplebuf std::this_thread::sleep_for(std::chrono::milliseconds( \
        std::rand() / (RAND_MAX / TEST_RACES_ntuplebuf_ms ) \
    ));
#else
#   define  YELD_ntuplebuf
#endif


namespace ntuplebuf {


constexpr int counter_bitsize(unsigned v){
    for(size_t i=0; i < sizeof(v) * CHAR_BIT; ++i){
        if(v < ((unsigned)1 << i)){
          return i;
        }
    }
    return sizeof(v) * CHAR_BIT;
}


template<typename ControlCodeT, unsigned NBUFS>  // ToDo:  + panic/warning handler?

struct NTupleBufferControl
{
    using CCodeT = ControlCodeT;

    enum: ControlCodeT{
        NumOfBuffers = NBUFS,
        count_bitsize = counter_bitsize(NBUFS),
        count_mask = (1 << count_bitsize) - 1
    };

    static_assert(
            std::is_integral<ControlCodeT>::value,
            "ControlCodeT shall be unsigned integral type"
    );

    static_assert(
            std::is_unsigned<ControlCodeT>::value,
            "ControlCodeT shall be unsigned"
    );

    static_assert( // space for buffers refcounts and current buffer number
            (NBUFS + 1)  * count_bitsize <= sizeof(ControlCodeT) * CHAR_BIT,
            "NBUFS too large for ControlCodeT"
    );

    int // returns positive (1-based number) on success, 0 if no data, negative on error
    start_reading(
            int* p_bufnum_prev // pointer to previous bufnum (1- based, may be 0 if no previous data)
                               // it will be released and  set to new bufnum
    ){
        ControlCodeT cco = cco_.load();
        for(;;){
            ControlCodeT new_cco = cco;
            ControlCodeT cur_bufnum = get_current(new_cco);
            if(cur_bufnum == 0){
                return 0; // no data
            }

            if(p_bufnum_prev != nullptr && dec_ref(new_cco, *p_bufnum_prev) < 0){
                return -3; // count overrun
            }

            if(inc_ref(new_cco, cur_bufnum) < 0){
                return -2; // count overrun
            }

            YELD_ntuplebuf

            if(cco_.compare_exchange_strong(cco, new_cco)){ //  weak would be sufficient?
                if(p_bufnum_prev != nullptr){
                    *p_bufnum_prev = (int)cur_bufnum;
                }
                return (int)cur_bufnum;
            }
        }

        return -100;// unreachable (calm compiler warning)
    }

    // Optional function that releases the consumed buffer after reading is over.
    // Call it if you want to release the buffer before the next start_reading() call.
    // start_reading() also releases the previous buffer, so usually
    // free() call is not necessary.
    int // returns new reference count value (>= 0) or negative on error
    free(
            int* p_bufnum // valid pointer to bufnum (to release); may point to 0 (no data);
                          // will be set to 0 (no data) on success
    ){
        if(p_bufnum == nullptr){
            return -11;
        }

        int bufnum = *p_bufnum;

        if(bufnum_valid(bufnum) <= 0){ // if error or no data
            return (bufnum == 0)? 0 : -14;
        }

        ControlCodeT cco = cco_.load();
        for(;;){
            ControlCodeT new_cco = cco;

            int count = dec_ref(new_cco, bufnum);
            if(count < 0){
                return -12; // count underrun
            }

            YELD_ntuplebuf

            if(cco_.compare_exchange_strong(cco, new_cco)){
                *p_bufnum = 0;
                return count;
            }
        }

        return -100;// unreachable (calm compiler warning)
    }


    // The function consume() acts as free() but also clears current buffer "pointer" if it is still the same.
    // That prevents future readers from processing the same buffer (they will see "no data")
    // Really useful only in case of one consumer since for several consumers the function just decrease the
    // probability of consuming the same buffer but does not exclude that.
    int // returns new reference count value (>= 0) or negative on error
    consume(
            int* p_bufnum // valid pointer to bufnum (to release); may point to 0 (no data);
                          // will be set to 0 (no data) on success
    ){
        if(p_bufnum == nullptr){
            return -21;
        }

        int bufnum = *p_bufnum;

        if(bufnum_valid(bufnum) <= 0){ // if error or no data
            return (bufnum == 0)? 0 : -22;
        }

        ControlCodeT cco = cco_.load();
        for(;;){
            ControlCodeT new_cco = cco;

            int count = dec_ref(new_cco, bufnum);
            if(count < 0){
                return -23; // count underrun
            }

            ControlCodeT cur_bufnum = get_current(new_cco);
            if((int)cur_bufnum == bufnum){ // if cur_bufnum not changed since reading started
                new_cco = set_current(new_cco, 0); // clear ("consume") current to prevent further reading of the same buffer
                    // (future readers will see "no data")

                count = dec_ref(new_cco, bufnum); // dereference "cleared" buffer
                if(count < 0){
                    return -24; // count underrun
                }
            }

            YELD_ntuplebuf

            if(cco_.compare_exchange_strong(cco, new_cco)){
                *p_bufnum = 0;
                return count;
            }
        }

        return -100;// unreachable (calm compiler warning)
    }


    int // returns positive (1-based number) on success, negative on error
    start_writing(
            int* p_bufnum_working // previous bufnum (1- based) to release and fill with new
    ){
        if(p_bufnum_working == nullptr || bufnum_valid(*p_bufnum_working) < 0){
            return -31;
        }

        int prev_bufnum =  *p_bufnum_working;

        ControlCodeT cco = cco_.load();
        for(;;){
            ControlCodeT new_cco = cco;

            if(prev_bufnum > 0){ // then commit previous buffer
                ControlCodeT cur_bufnum = get_current(new_cco);

                if(dec_ref(new_cco, cur_bufnum) < 0){ // deref old current
                    return -32;
                }
                new_cco = set_current(new_cco, prev_bufnum);
                // reference count for prev_bufnum remains the same (transfered from working to recent)
            }

            int new_bufnum = find_new(new_cco);
            if(new_bufnum < 1){
                return -35; // not found
            }

            inc_ref(new_cco, new_bufnum);

            YELD_ntuplebuf

            if(cco_.compare_exchange_strong(cco, new_cco)){ //  weak would be sufficient?
                *p_bufnum_working = (int)new_bufnum;
                return (int)new_bufnum;
            }
        }

        return -100;// unreachable (calm compiler warning)
    }

    int // returns 0 on success, negative on error
    commit(
            int* p_bufnum_working //  bufnum (1- based) to release and to fill with new
    ){
        if(p_bufnum_working == nullptr || bufnum_valid(*p_bufnum_working) < 0){
            return -41;
        }

        int prev_bufnum =  *p_bufnum_working;
        if(prev_bufnum == 0){
            return 0; // nothing to commit
        }

        ControlCodeT cco = cco_.load();
        for(;;){
            ControlCodeT new_cco = cco;

            ControlCodeT cur_bufnum = get_current(new_cco);


            if(dec_ref(new_cco, cur_bufnum) <0){
                return -42;
            }
            new_cco = set_current(new_cco, prev_bufnum);
            // reference count for prev_bufnum remains the same (transfered from working to recent)

            YELD_ntuplebuf

            if(cco_.compare_exchange_strong(cco, new_cco)){ //  weak would be sufficient?
                *p_bufnum_working = 0; // just clear
                return 0;
            }
        }

        return -100;// unreachable (calm compiler warning)
    }


private:
    // everywhere: bufnum is 1-based number of a buffer; buf_idx is 0-based index (bufnum == buf_idx + 1)

    int  // negativ if invalid; otherwise bufnum itself (may be 0 if no data)
    bufnum_valid(int bufnum){
        return (bufnum < 0 || bufnum > (int)NBUFS)? -1 : bufnum;
    }

    ControlCodeT get_count(ControlCodeT cco, int buf_idx){return (cco >> (buf_idx * count_bitsize)) & count_mask; }
    ControlCodeT set_count(ControlCodeT cco, int buf_idx, ControlCodeT val){
        int pos = buf_idx * count_bitsize;
        ControlCodeT m = count_mask << pos;
        return (cco & ~m) | (m & (val << pos));
    }

    // the 2 functions get or set 1-based number of current buffer:
    ControlCodeT get_current(ControlCodeT cco){return get_count(cco, NBUFS);}
    ControlCodeT set_current(ControlCodeT cco, ControlCodeT val){ return set_count(cco, NBUFS, val);}

    int  // returns bufnum (1-based) 0 if not found
    find_new(ControlCodeT cco){
        ControlCodeT m = count_mask;
        for(unsigned i =0; i < NBUFS; ++i){
            if((cco & m) == 0){
                return i + 1 ; // convert index to 1-based
            }

            m = m << count_bitsize;
        }

        return 0; // not found
    }

    int inc_ref(ControlCodeT& cco, int bufnum){
        if(bufnum <= 0){
            return 0;
        }

        int buf_idx = bufnum -1;
        ControlCodeT count = get_count(cco, buf_idx);
        if(count >= NBUFS){
            return -2; // count overrun
        }

        count++;
        cco = set_count(cco, buf_idx, count);

        return (int) count;
    }

    int dec_ref(ControlCodeT& cco, int bufnum){
        if(bufnum <= 0){
            return 0;
        }

        int buf_idx = bufnum -1;
        ControlCodeT count = get_count(cco, buf_idx);
        if(count == 0){
            return -2; // count underrun
        }

        count--;
        cco = set_count(cco, buf_idx, count);

        return (int) count;
    }


    std::atomic<ControlCodeT> cco_ = {0};
};



} // namespace

#endif
