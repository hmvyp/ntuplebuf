#ifndef ntuplebuf_test_hpp
#define ntuplebuf_test_hpp

#define TEST_RACES_ntuplebuf 1

#include "ntuplebuf_dyn.hpp"
#include <array>
#include <iostream>
#include <thread>
#include <mutex>


struct NtbTesBase{
    template<typename Func>
    static void under_lock(Func f){
        static std::mutex mux_;
        std::lock_guard<decltype(mux_)> g(mux_);

        f();
    }
};


template <typename ControlCodeT, unsigned NConsumers>
struct NtbTestMT
        : public NtbTesBase
{
    enum ProducerMode{
        P_SIMPLE,
        COMMIT
    };

    enum ConsumerMode{
        C_SIMPLE,
        FREE,
        CONSUME
    };

    NtbTestMT(unsigned cycles, ProducerMode pm = P_SIMPLE, ConsumerMode cm = C_SIMPLE)
    : prod_cycles_(cycles), pm_(pm), cm_(cm)
    {}

    struct Data{
        unsigned count;
    };

    void start(){

        under_lock([=](){
            std::cout << "\n\n===== staring test ====== consumers: " << NConsumers <<" cycles: " << prod_cycles_ << " pm: " << pm_ << " cm: " << cm_ << "\n";
        });

        std::thread cons_threads[NConsumers];
        for(unsigned i = 0; i < NConsumers; ++i){
            cons_threads[i] = std::move(std::thread([=](){ this->consumer(i);}));
        }

        producer();

        for(unsigned i = 0; i < NConsumers; ++i){
            cons_threads[i].join();
        }
    }

    void consumer(int consNo){
        Data* p = nullptr;

        while(!stop_.load()){
            auto res = nbc.start_reading((void**)&p);
            if(res < 0){
                under_lock([=](){
                std::cout << "** Read error consNo: " << consNo << "  error: " << res << "\n";
                });
                break;
            }else{
                under_lock([=](){
                    if(p != nullptr){
                        under_lock([=](){
                        std::cout << "consNo: " << consNo << " data: ";
                        std::cout << p->count;
                        std::cout << "\n";
                        });
                    }else{
                        under_lock([=](){
                        std::cout << "consNo: " << consNo;
                        std::cout << " no data \n";
                        });
                        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // duck!!!
                    }
                });

                switch(cm_){
                case FREE:
                    res = nbc.free((void**)&p);
                    break;
                case CONSUME:
                    res = nbc.consume((void**)&p);
                    break;
                case C_SIMPLE:
                default:
                    break;
                }

                if(res < 0){
                    under_lock([=](){
                    std::cout << "** Read error consNo: " << consNo << "  error: " << res << "\n";
                    });
                    break;
                }
            }
        }
    }

    void producer(){
        unsigned count = 10;
        Data* p = nullptr;

        for(unsigned i = 0; i < prod_cycles_; ++i){
            auto res = nbc.start_writing((void**)&p);
            if(res < 0){
                std::cout << "** Read error Producer: " << "  error: "<< res << "\n";
                break;
            }

            {
                p->count = ++count;
                under_lock([=](){
                    std::cout << "Producer:  data: " << count << "\n";
                });
            }

            if(pm_ == COMMIT){
                res = nbc.commit((void**)&p);
                if(res < 0){
                    under_lock([=](){
                    std::cout << "** Read error Producer: " << "  error: "<< res << "\n";
                    });
                    break;
                }
            }

        }

        under_lock([=](){
        std::cout << "producer: sleeping... \n ";
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        under_lock([=](){
        std::cout << "producer: stopping all\n ";
        });


        stop_.store(true);
    }

private:

    /*
    template<typename Func>
    void under_lock(Func f){
        std::lock_guard<decltype(mux_)> g(mux_);

        f();
    }
    */

    ntuplebuf::NTupleBufferDynAlloc<ControlCodeT, NConsumers + 2> nbc = {sizeof(Data)};

    std::atomic<bool> stop_ = {0};

    // std::mutex mux_;

    unsigned prod_cycles_;

    ProducerMode pm_;
    ConsumerMode cm_;


};

int ntuplebuf_test(){
    ntuplebuf::NTupleBufferControl<unsigned, 7> nbc;
    // ntuplebuf::NTupleBufferControl<unsigned long, 8> nbc; // convinient to debug
    // ntuplebuf::NTupleBufferControl<unsigned, 8> nbc_err_1;
    // ntuplebuf::NTupleBufferControl<int, 7> nbc_err_2;


    int wb = 0;
    int rb = 0;
    nbc.start_writing(&wb);
    nbc.commit(&wb);
    nbc.start_reading(&rb);
    nbc.free(&rb);
    nbc.start_writing(&wb);
    nbc.start_reading(&rb);
    nbc.start_writing(&wb);
    nbc.start_reading(&rb);
    nbc.start_writing(&wb);
    nbc.start_reading(&rb);
    nbc.start_writing(&wb);
    nbc.start_reading(&rb);

    typedef NtbTestMT<unsigned, 5> T5;
    typedef NtbTestMT<unsigned, 1> T1;

    /*
    {
    T5 tst(20);
    tst.start();
    }

    {
    T1 tst(50, T1::COMMIT, T1::C_SIMPLE);
    tst.start();
    }

    {
    T5 tst(20, T5::COMMIT, T5::FREE);
    tst.start();
    }
    */
    {
        T1 tst(50, T1::COMMIT, T1::CONSUME);
        //T1 tst(50, T1::COMMIT, T1::C_SIMPLE);
        // T1 tst(50, T1::P_SIMPLE, T1::CONSUME);
        tst.start();
    }


    return 0;
}


#endif
