#ifndef ntuplebuf_test_hpp
#define ntuplebuf_test_hpp


// up to ... milliseconds sleep in selected points of the algorithm:
//#define TEST_RACES_ntuplebuf_ms 100

#include <array>
#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <unistd.h>

#include "test_scheduler.hpp"

using Shed = lf_test_utils::SequentialThreadsSched;
using Alg = lf_test_utils::SimpleRandomAlgorithm;

static std::unique_ptr<Shed> psched;

#define YELD_ntuplebuf psched->yeld();
// #define DBG_STATUS_ntuplebuf

#include "ntuplebuf_dyn.hpp"



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
    std::string s{"this is string data"};
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
        COMMIT,
        TRANSACT
    };

    enum ConsumerMode{
        C_SIMPLE,
        FREE,
        CONSUME,
        POP
    };

    NtbTestMT(unsigned cycles, ProducerMode pm = P_SIMPLE, ConsumerMode cm = C_SIMPLE)
    : prod_cycles_(cycles), pm_(pm), cm_(cm)
    {
        psched = std::unique_ptr<Shed>(new Shed(
                std::shared_ptr<Alg>(new Alg(0.9)))
        );


    }

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

        sleep(1); // allow consumer threads to start (for deterministic test behavior)

        producer();

        for(unsigned i = 0; i < NConsumers; ++i){
            cons_threads[i].join();
        }
    }

    void consumer(int consNo){
        psched->add_thread();

        DataT* p = nullptr;

        while(!stop_.load()){
            auto res = (cm_ == POP)? nbc.pop(&p) : nbc.start_reading(&p);
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
                        if(cm_ == POP){
                            std::cout << " String received: " << p->s;
                        }
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
                case POP:
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

        psched->remove_thread();

    }

    void producer(){
        psched->add_thread();
        psched->start();

        unsigned count = 10;
        DataT* p = nullptr;

        for(unsigned i = 0; i < prod_cycles_; ++i){

            if(pm_ == TRANSACT){
                bool error = false;
                ++count;
                auto add_s = std::string("_") + std::to_string(count);

                for(int j = 0 ; j != 10; ++j){ // transaction attempts
                    bool collision = false;

                    auto tra = nbc.start_transaction();
                    if(tra.errcode < 0){
                        error = true;
                        std::cout << "*** Producer: Transaction start error: " << "  error: "<< tra.errcode << "\n";
                        break;
                    }

                    tra.new_buf->count = count;
                    tra.new_buf->s = ((tra.old_buf != nullptr)?  tra.old_buf->s : std::string()) + add_s;

                    auto comm_res = nbc.commit_transaction(tra, false);

                    switch(comm_res){
                    case 0:
                        under_lock([=](){
                            std::cout << "Producer: Transaction succeeds \n";
                        });
                        break;
                    case 1:
                        collision = true;

                        under_lock([=](){
                            std::cout << "Producer: <==> Transaction collision \n";
                        });
                        break;
                    default:
                        error = true;
                        under_lock([=](){
                            std::cout << "*** Producer: Transaction commit error" << comm_res << "\n";
                        });
                    }

                    if(collision){
                        continue;
                    }else{
                        break;
                    }
                } // for until transaction success success

                if(error){
                    break;
                }

            }else{
                auto res = nbc.start_writing(&p);
                if(res < 0){
                    std::cout << "** Write error Producer: " << "  error: "<< res << "\n";
                    break;
                }

                {
                    p->count = ++count;
                    under_lock([=](){
                        std::cout << "Producer prepared  data: " << count << "\n";
                    });
                }

                if(pm_ == COMMIT){
                    res = nbc.commit(&p);
                    if(res < 0){
                        under_lock([=](){
                        std::cout << "** Read error Producer: " << "  error: "<< res << "\n";
                        });
                        break;
                    }else{
                        under_lock([=](){
                            std::cout << "Producer commited  data: " << count << "\n";
                        });
                    }
                }
            }
        }

        under_lock([=](){
        std::cout << "producer: sleeping... \n ";
        });

        // allow consumers to consume last message:
        // std::this_thread::sleep_for(std::chrono::milliseconds(TEST_RACES_ntuplebuf_ms * 3));


        for(int i = 0; i < 10; ++i){
            YELD_ntuplebuf;
        }

        under_lock([=](){
        std::cout << "producer: stopping all\n ";
        });

        psched->remove_thread();

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

    /*
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
*/
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
        T1 tst(20, T1::COMMIT, T1::C_SIMPLE);
        //T1 tst(50, T1::P_SIMPLE, T1::CONSUME);
        tst.start();
    }


    {
        //T1 tst(50, T1::COMMIT, T1::CONSUME);

        //T1 tst(1, T1::P_SIMPLE, T1::C_SIMPLE);
        //T1 tst(10, T1::COMMIT, T1::C_SIMPLE);

        //T1 tst(1, T1::P_SIMPLE, T1::CONSUME);
        T1 tst(10, T1::COMMIT, T1::CONSUME);
        tst.start();
    }

    {
        // T1 tst(10, T1::COMMIT, T1::POP);
        T1 tst(50, T1::TRANSACT, T1::POP);
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
