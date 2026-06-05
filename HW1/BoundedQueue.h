/*
Written by Yariv Aridor, 2022
*/

#pragma once


class BoundedQueueAbstract {

public:
    // Return the number of elements in the queue
    virtual int size() =0 ;

    // Pop the next element (integer value) from the queue.
    // if the buffer is empty, the calling thread waits until being notified of new elements in the queue
    virtual int pop() =0 ;
    
    // Push a new integer to the queue.
    // if the buffer is full, the calling thread should wait until being notified that the queue is not full anymore
    // v - the new integer to push into the queue.
    virtual void push(int v) =0;
};
