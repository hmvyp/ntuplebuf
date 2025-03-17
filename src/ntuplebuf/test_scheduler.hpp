#ifndef lockfree_test_scheduler_hpp
#define lockfree_test_scheduler_hpp

#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdlib>
#include <memory>


namespace lf_test_utils {

struct ShedAlgorithmIface{

    virtual int choose(int num_of_threads, int curent_thread_num) = 0;

    virtual ~ShedAlgorithmIface(){}
};

/*
int num_of_ones(unsigned mask){
    int cur_numOf1 = -1;
    for(int i = 0; i != sizeof(mask) * 8; ++i){
        if((((decltype(mask))1 << i) & mask) == 0){
            continue;
        }

        cur_numOf1++;
    }

    return cur_numOf1 + 1;
}

int pos_in_mask(unsigned mask, int numOf1){
    int cur_numOf1 = -1;
    for(int i = 0; i != sizeof(mask) * 8; ++i){
        if((((decltype(mask))1 << i) & mask) == 0){
            continue;
        }

        cur_numOf1++;
        if(cur_numOf1 == numOf1){
            return i;
        }
    }

    return -1;
}
*/

struct SimpleRandomAlgorithm
    : public ShedAlgorithmIface
{
    SimpleRandomAlgorithm(double switch_probability)
        : switch_prob_(switch_probability)
    {}

    int
    choose(int num_of_threads, int curent_thread_num) override
    {
        auto swtch = drand();
        if(swtch > switch_prob_){
            return curent_thread_num;
        }

        auto choice = drand();
        int ret = (int) (choice * num_of_threads);
        return ret < num_of_threads? ret : 0 ;
    }

private:
    double drand(){
        return  (double)std::rand() / (double)RAND_MAX;
    }

    double switch_prob_ = 0.5;
};


thread_local int cur_thread_num;


class SequentialThreadsSched{
public:

    SequentialThreadsSched(std::shared_ptr<ShedAlgorithmIface> alg)
    : alg_(alg)
    {};

    void add_thread(){
        cur_thread_num = num_of_threads_;
        threads_mask_ = threads_mask_ | ((decltype(threads_mask_))1 << num_of_threads_);
        num_of_threads_ ++;
    }

    void remove_thread(){
        threads_mask_ = threads_mask_ & ~((decltype(threads_mask_))1 << cur_thread_num);
    }

    void start(){
        std::unique_lock<decltype(mux_)> lk(mux_);
        this->next_active_thread_num_ = cur_thread_num;
        condvar_.notify_all();
    }

    void yeld(){
        std::unique_lock<decltype(mux_)> lk(mux_);

        for(;;){
            if(next_active_thread_num_ == cur_thread_num){
                prev_active_thread_num_ = next_active_thread_num_;
                // calculate the thread scheduled on next yeld():
                for(;;){
                    auto tmp = alg_->choose(this->num_of_threads_, cur_thread_num);

                    if( (threads_mask_ & ((decltype(threads_mask_))1 << tmp)) == 0){
                        continue; // if chosen thread deleted
                    }else{
                        this->next_active_thread_num_ = tmp;
                        break;
                    }
                }

                return;
            }

            if(prev_active_thread_num_ == cur_thread_num){
                condvar_.notify_all();
            }

            condvar_.wait(lk);
        }
    }


protected:
    int next_active_thread_num_ = -1;
    int prev_active_thread_num_ = -1;
    std::mutex mux_;
    std::condition_variable condvar_;

    int num_of_threads_ = 0;

    unsigned threads_mask_ = 0;

    std::shared_ptr<ShedAlgorithmIface> alg_;
};


} // namespace
#endif
