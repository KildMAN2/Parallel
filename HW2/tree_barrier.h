class BinaryTreeBarrierAbstract {
    public:
        virtual void barrier() = 0;
};


// assume that all N threads using the tree barrier have an unique id in the range [0..N-1]
thread_local int thread_id;
