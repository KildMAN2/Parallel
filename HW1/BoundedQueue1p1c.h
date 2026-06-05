/*
Written by Yariv Aridor, 2022
*/

class BoundedQueueAbstract_1p1c {

public:

    // Return the number of elements in the queue
    virtual int size() =0;

    // Pop the next element (integer value) from the queue.
    // if the buffer is empty, ruturn_false. Otherwise, true.
    virtual bool pop(int &val) =0;

    // Push a new integer to the queue.
    // if queue is full, return false. Otherwise, true.
    // v - the new integer to push into the queue.
    virtual bool push(int v) =0;
};
