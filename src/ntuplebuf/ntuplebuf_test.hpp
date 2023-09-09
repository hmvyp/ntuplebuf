#ifndef ntuplebuf_test_hpp
#define ntuplebuf_test_hpp


// up to ... milliseconds sleep in selected points of the algorithm:
#define TEST_RACES_ntuplebuf_ms 100

#include "ntuplebuf_dyn.hpp"
#include <array>
#include <iostream>
#include <thread>
#include <mutex>
#include <string>


struct NtbTesBase{
    template<typename Func>
    static void under_lock(Func f){
        static std::mutex mux_;
        std::lock_guard<decltype(mux_)> g(mux_);

        f();
    }
};

struct DataBase{
    unsigned count = 0;
    static void printInstancesCounter(){};
};


struct Data
: public DataBase{

    Data()
    {
        ninstances ++;
    }

    ~Data(){
        ninstances --;
    }


    static std::atomic<unsigned> ninstances;

    static void printInstancesCounter(){
        NtbTesBase::under_lock([=](){
            std::cout << (
                    std::string("\n instances counter: ")
                    + std::to_string(Data::ninstances.load())
                    + "\n\n"
            );
        });
    }


};

std::atomic<unsigned> Data::ninstances;


template <typename ControlCodeT, unsigned NConsumers, typename DataT>
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

    ~NtbTestMT()
    {
        DataT::printInstancesCounter();
        /*
        under_lock([=](){
            std::cout << (
                    std::string("\n instances counter: ")
                    + std::to_string(Data::ninstances.load())
                    + "\n\n\n"
            );
        });
        */
    }

    void start(){

        under_lock([=](){
            std::cout << (
                    std::string("\n\n===== staring test ====== consumers: ") + std::to_string(NConsumers)
                    + "   cycles: " + std::to_string(prod_cycles_)
                    + "   pm: " + std::to_string(pm_)
                    + "   cm: " + std::to_string(cm_) + "\n"
                    //+ "data instances counter: " + std::to_string(Data::ninstances.load()) + "\n"
            );
        });

        DataT::printInstancesCounter();

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
        DataT* p = nullptr;

        while(!stop_.load()){
            auto res = nbc.start_reading(&p);
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
                        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // do not annoy console
                    }
                });

                switch(cm_){
                case FREE:
                    res = nbc.free(&p);
                    break;
                case CONSUME:
                    res = nbc.consume(&p);
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
        DataT* p = nullptr;

        for(unsigned i = 0; i < prod_cycles_; ++i){
            auto res = nbc.start_writing(&p);
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
                res = nbc.commit(&p);
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

        // allow consumers to consume last message:
        std::this_thread::sleep_for(std::chrono::milliseconds(TEST_RACES_ntuplebuf_ms * 3));

        under_lock([=](){
        std::cout << "producer: stopping all\n ";
        });


        stop_.store(true);
    }

private:

    ntuplebuf::NTupleBufferDynAllocTyped<ControlCodeT, NConsumers + 2, DataT> nbc; // = {sizeof(Data)};

    std::atomic<bool> stop_ = {0};

    unsigned prod_cycles_;

    ProducerMode pm_;
    ConsumerMode cm_;
};

int ntuplebuf_test(){
    ntuplebuf::NTupleBufferControl<unsigned, 7> nbc;
    // ntuplebuf::NTupleBufferControl<unsigned long, 8> nbc; // convinient to debug

#   if 0
    // the following declarations produce design time errors:

    ntuplebuf::NTupleBufferControl<unsigned, 8> nbc_err_1;
    ntuplebuf::NTupleBufferControl<int, 7> nbc_err_2;
#   endif

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

    typedef NtbTestMT<unsigned, 5, DataBase> T5;
    typedef NtbTestMT<unsigned, 1, Data> T1;

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

    {
        //T1 tst(50, T1::COMMIT, T1::CONSUME);
        //T1 tst(50, T1::COMMIT, T1::C_SIMPLE);
        T1 tst(50, T1::P_SIMPLE, T1::CONSUME);
        tst.start();
    }

    std::cout << (
            std::string("\n\n\n ========================\n tests destroyed.  Data instances counter: ")
            + std::to_string(Data::ninstances.load())
            + "\n\n\n"
    );

    return 0;
}


#endif
