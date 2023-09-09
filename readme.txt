
Triple buffer is an implementation technique for producer-consumer pattern using shared memory without need of any data copying. The well known application of this technique is data transfer between CPU and video subsystem, but it is also very efficient for various real time (including hard real time) software systems.

The main idea of triple buffer is quite simple: while producer fills the output message, consumer consumes another message (which was «most recent» when consumer started consumption). While consuming and producing are in progress, the last already commited message remains untouched in the third message buffer (it is always ready to be consumed). Thus, three message buffers are always sufficient for 1 consumer and 1 producer (this explains the term «triple buffer»). Generally, for N participants N + 1 message buffer needed. 

The main assumptions for using triple buffer are as follows.

1) Data transfer between producer and consumer is message-oriented with fixed-size (or limited-size) messages.
2) Consumer is only interested in most recent data message written by the producer.

In other words, older messages, even unread, are of no interest to the consumer.

Note, in general case of several consumers, some consumers may share the same message buffer (they neither change  nor «remove» the consumed message).

Conceptually, data transfer satisfying the assumptions above may be implemented using 

std::atomic<std::shared_ptr<MessageDataType> >

Indeed, producer may allocate every new message from the heap, then fill it with data, then atomically store the message as atomic shared pointer. Consumer, in its turn, can atomically load shared pointer, process the message and then destroy the loaded (its local, non-atomic copy of) shared_ptr after processing.

Such implementation works with unlimited set of producers and consumers (and even with unlimited message size) but also has obvious drawbacks: costly and unpredictible heap memory allocation. Moreover, the implementation of atomic shared_ptr is quite complex and even may be lock-based (not lock-free). These drawbacks are significant in hard real time environment. 

Triple buffer implementation presented here can be treated conceptually as a special case of the atomic shared_ptr implementation described above. The main difference is that the memory for messages is allocated only once (at the construction time of the triple buffer). The other difference is that reference counters and «atomic pointer» itself are combined into single atomic integral type. The latter allows very simple and efficient lock-free implementation.

There are, however, some limitations. The first (as already mentioned) is fixed size (or size limit) of messages. The second is maximum number of participants limited by atomic integral type capacity. In typical cases, 32-bit atomic limits the number of buffers by 7 (6 participants allowed), 64-bit atomic limits the number of buffers by 15 (14 participants allowed).

