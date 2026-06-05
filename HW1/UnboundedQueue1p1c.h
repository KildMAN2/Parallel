/*
Written by Yariv Aridor, 2022
*/

class UnboundedQueue1p1cAbstract {

public:

    // Pop the next element (integer value) from the queue.
    // if the buffer is empty, ruturn_false. Otherwise, true.
    virtual bool pop(int &val) =0;

    // Push a new integer to the queue.
    // v - the new integer to push into the queue.
    virtual void  push(int value) =0;

    // Return the number of elements in the queue
    virtual int size() const =0;
};
