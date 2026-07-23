  class SkipList {
    public:
      // Constructor 
      SkipList()=delete;
      SkipList(int maxLevel):m_maxLevel(maxLevel){}
      ~SkipList()=default;
    
      // Insert Node to maintain ordered skip-list
      virtual void insert(int value)=0;
      // Search for node, return true if found
      virtual bool search(int value)=0;
      // Deletes node from skip list
      virtual void remove(int value)=0;
    protected:
     int m_maxLevel;
   };
   