#ifndef ntuplebuf_ntuplebuf_h
#define ntuplebuf_ntuplebuf_h

#include <climits>
#include <type_traits>
#include <atomic>


namespace ntuplebuf {


constexpr int bitsize(unsigned v){
    for(int i=0; i < sizeof(v) * CHAR_BIT; ++i){
        if(v < (1 << i)){
          return i;
        }
    }
    return sizeof(v) * CHAR_BIT;
}


template<typename ControlCodeT, unsigned NBUFS>  // ToDo:  + panic/warning handler?

struct NTupleBufferControl
{
    enum: ControlCodeT{
        count_bitsize = bitsize(NBUFS),
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

    static_assert(
            (NBUFS + 1)  * count_bitsize <= sizeof(ControlCodeT) * CHAR_BIT,
            "NBUFS too large for ControlCodeT"
    );

    int start_read(){
        ControlCodeT cco = cco_.load();
        for(;;){
            auto cur = get_current(cco);
            if(cur == 0){
                return -1; // no data
            }

            int buf_idx = cur - 1;  // convert to 0-based index
            ControlCodeT count = get_count(cco, buf_idx) + 1;
            if(count >= NBUFS){
                return -2; // count overrun
            }

            ControlCodeT new_cco = set_count(cco, buf_idx, count);

            if(cco_.compare_exchange_strong(cco, new_cco)){
                return cur;
            }
        }

        return -100;// unreachable (calm compiler warning)

    }

    int free(int bufnum){ // 1- based; returns new count or negative if error

        if(bufnum < 1 || bufnum > NBUFS){
            return -1;
        }

        int buf_idx = bufnum - 1;  // convert to 0-based index

        ControlCodeT cco = cco_.load();
        for(;;){
            ControlCodeT count = get_count(cco, buf_idx);
            if(count == 0){
                return -2; // count underrun
            }

            count--;
            ControlCodeT new_cco = set_count(cco, buf_idx, count);

            if(cco_.compare_exchange_strong(cco, new_cco)){
                return (int)count;
            }
        }

        return -100;// unreachable (calm compiler warning)
    }

private:
    ControlCodeT get_count(ControlCodeT cco, int buf_idx){return (cco >> (buf_idx * count_bitsize)) & count_mask; }
    ControlCodeT set_count(ControlCodeT cco, int buf_idx, ControlCodeT val){
        int pos = buf_idx * count_bitsize;
        ControlCodeT m = count_mask << pos;
        return (cco & ~m) | (m & (val << pos));
    }

    // the 2 functions get or set 1-based number of current buffer:
    ControlCodeT get_current(ControlCodeT cco){return get_count(cco, NBUFS);}
    ControlCodeT set_current(ControlCodeT cco, ControlCodeT val){ return set_count(cco, NBUFS, val);}

    std::atomic<ControlCodeT> cco_ = 0;
};



} // namespace

#endif
