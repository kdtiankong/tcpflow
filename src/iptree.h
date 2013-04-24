/*
 * iptree.h:
 * 
 * Maintains a count of all IP addresses seen, with limits on the
 * maximum amount of memory.
 *
 * #include this file after config.h (or whatever you are calling it)
 */

#ifndef IPTREE_H
#define IPTREE_H

#include <stdint.h>
#include <algorithm>
#include <assert.h>
#include <iostream>
#include <iomanip>

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#define IP4_ADDR_LEN 4
#define IP6_ADDR_LEN 16

/**
 * the iptree.
 *
 * pruning a node means cutting off its leaves (the node remains in the tree).
 */

/* addrbytes is the number of bytes in the address */

template <typename TYPE,size_t ADDRBYTES> class iptreet {
private:;
    class not_impl: public std::exception {
	virtual const char *what() const throw() { return "copying iptreet objects is not implemented."; }
    };
    /**
     * the node class.
     * Each node tracks the sum that it currently has and its two children.
     * A node has pointers to the 0 and 1 children, as well as a sum for everything below.
     * A short address or prefix being tallied may result in BOTH a sum and one or more PTR values.
     * If a node is pruned, ptr0=ptr1=0 and tsum>0.  
     * If tsum>0 and ptr0=0 and ptr1=0, then the node cannot be extended.
     * Nodes need to know their parent so that nodes found through the cache can be made dirty,
     * which requires knowing their parents.
     */
    class node {
    private:
        /* Assignment is not implemented */
        node &operator=(const iptreet::node &that){
            throw not_impl();
        }
        /* copy is no longer implemented, because it's hard to do with the parents */
        node(const node &n){
            throw not_impl();
        }
    public:
        class node *parent;
        class node *ptr0;               // 0 bit next
        class node *ptr1;               // 1 bit next
    private:
        TYPE    tsum;                   // this node and pruned children.
        bool    dirty;                  // add() has been called and cached data is no longer valid
    public:
        node(node *p):parent(p),ptr0(0),ptr1(0),tsum(),dirty(false){ }
        int children() const {return (ptr0 ? 1 : 0) + (ptr1 ? 1 : 0);}
        ~node(){
            if(ptr0){ delete ptr0; ptr0 = 0; }
            if(ptr1){ delete ptr1; ptr1 = 0; }
        };
        // a node is terminal if tsum>0 and both ptrs are 0.
        bool term() const {             
            if(tsum>0 && ptr0==0 && ptr1==0) return true;
            return false;
        }
        /**
         * Returns number of nodes pruned.
         * But this is called on a node! 
         * So this *always* returns 1.
         * If it is not, we have an implementation error, because prune() should not have been called.
         */
        int prune(class iptreet &tree){                    // prune this node
            /* If prune() on a node is called, then both ptr0 and ptr1 nodes, if present,
             * must not have children.
             * Now delete those that we counted out
             */
            if(ptr0){
                assert(ptr0->term());   // only prune terminal nodes
                tsum += ptr0->tsum;
                tree.cache_remove(ptr0); // remove it from the cache
                tree.pruned++;
                delete ptr0;
                ptr0=0;
                tree.nodes--;
            }
            if(ptr1){
                assert(ptr1->term()); 
                tsum += ptr1->tsum;
                tree.cache_remove(ptr1);
                tree.pruned++;
                delete ptr1;
                ptr1=0;
                tree.nodes--;
            }
            return 1;
        }

        /** best describes the best node to prune */
        class best {
        private:
            best &operator=(const iptreet::node::best &that){
                throw not_impl();
            }
        public:
            const node *best_node;
            int depth;
            best(const node *best_node_,int depth_):
                best_node(best_node_),depth(depth_){}
            best(const best &b):best_node(b.best_node),depth(b.depth){ }
            virtual ~best(){
                best_node=0;
                depth=0;
            }
            std::ostream & dump(std::ostream &os) const {
                os << "node=" << best_node << " depth=" << depth << " ";
                return os;
            }
        };

        /**
         * Return the best node to prune (the node with the leaves to remove)
         * Possible outputs:
         * case 1 - no node (if this is a terminal node, it can't be pruned; should not have been called)
         * case 2 - this node (if all of the children are terminal)
         * case 3 - the best node of the one child (if there is only one child)
         * case 4 - the of the non-terminal child (if one child is terminal and one is not)
         * case 5 - the better node of each child's best node.
         */
            
        class best best_to_prune(int my_depth) const {
            assert(term()==0);          // case 1
            if (ptr0 && ptr0->term() && !ptr1)  return best(this,my_depth); // case 2
            if (ptr1 && ptr1->term() && !ptr0 ) return best(this,my_depth); // case 2
            if (ptr0 && ptr0->term() && ptr1 && ptr1->term()) return best(this,my_depth); // case 2
            if (ptr0 && !ptr1) return ptr0->best_to_prune(my_depth+1); // case 3
            if (ptr1 && !ptr0) return ptr1->best_to_prune(my_depth+1); // case 3

            if (ptr0->term() && !ptr1->term()) return ptr1->best_to_prune(my_depth+1); // case 4
            if (ptr1->term() && !ptr0->term()) return ptr0->best_to_prune(my_depth+1); // case 4

            // case 5 - the better node of each child's best node.
            best ptr0_best = ptr0->best_to_prune(my_depth+1);
            best ptr1_best = ptr1->best_to_prune(my_depth+1);

            // The better to prune of two children is the one with a lower sum.
            TYPE ptr0_best_sum = ptr0_best.best_node->sum();
            TYPE ptr1_best_sum = ptr1_best.best_node->sum();
            if(ptr0_best_sum < ptr1_best_sum) return ptr0_best;
            if(ptr1_best_sum < ptr0_best_sum) return ptr1_best;
            
            // If they are equal, it's the one that's deeper
            if(ptr0_best.depth > ptr1_best.depth) return ptr0_best;
            return ptr1_best;
        }

        /** The nodesum is the sum of just the node.
         * This exists purely because tsum is a private variable.
         */
        TYPE nodesum() const {
            return tsum;
        }

        /** The sum is the sum of this node and its children (if they exist) */
        TYPE sum() const {
            TYPE s = tsum;
            if(ptr0) s+=ptr0->sum();
            if(ptr1) s+=ptr1->sum();
            return s;
        }
        /** Increment this node by the given amount */
        void add(TYPE val) {
            tsum+=val;                  // increment
            set_dirty();
        }          

        void set_dirty() {              // make us dirty and our parent dirty
            if(dirty==false){
                dirty = true;
                if(parent && parent->dirty==false){
                    parent->set_dirty();    // recurses to the root or the first dirty node.
                } 
            }
        }

    }; /* end of node class */
    class node *root;                  
    enum {root_depth=0,
          max_histogram_depth=128,
          ipv4_bits=32,
          ipv6_bits=128,
    };
    iptreet &operator=(const iptreet &that){throw not_impl();}
protected:
    size_t     nodes;                   // nodes in tree
    size_t     maxnodes;                // how many will we tolerate?
    uint64_t   ctr_added;                   // how many were added
    uint64_t   pruned;
public:


    /****************************************************************
     *** static member service routines
     ****************************************************************/

    /* get the ith bit; 0 is the MSB */
    static bool bit(const uint8_t *addr,size_t i){
        return (addr[i / 8]) & (1<<((7-i)&7));
    }
    /* set the ith bit to 1 */
    static void setbit(uint8_t *addr,size_t i){
        addr[i / 8] |= (1<<((7-i)&7));
    }
    
    virtual ~iptreet(){}                // required per compiler warnings
    /* copy is a deep copy */
    iptreet(const iptreet &n):root(n.root ? new node(*n.root) : 0),
                              nodes(n.nodes),maxnodes(n.maxnodes),ctr_added(),pruned(),cache(),cachenext(),cache_hits(),cache_misses(){};

    /* create an empty tree */
    iptreet(int maxnodes_):root(new node(0)),nodes(0),maxnodes(maxnodes_),
                           ctr_added(),pruned(),cache(),cachenext(),cache_hits(),cache_misses(){
        for(size_t i=0;i<cache_size;i++){
            cache.push_back(cache_element(0,0,0));
        }
    };

    /* size the tree; the number of nodes */
    size_t size() const {return nodes;};

    /* sum the tree; the total number of adds that have been performed */
    TYPE sum() const {return root->sum();};

    /* add a node; implementation below */
    void add(const uint8_t *addr,size_t addrlen,TYPE val); 

    /****************************************************************
     *** cache
     ****************************************************************/
    class cache_element {
    public:
        uint8_t addr[ADDRBYTES];
        node *ptr;                      // 0 means cache entry is not in use
        cache_element(const uint8_t addr_[ADDRBYTES],size_t addrlen,node *p):addr(),ptr(p){
            memcpy(addr,addr_,addrlen);
        }
    };
    enum {cache_size=4};
    typedef std::vector<cache_element> cache_t;
    cache_t cache;
    size_t cachenext;                   // which cache element to evict next
    uint64_t cache_hits;
    uint64_t cache_misses;

    void cache_remove(const node *p){
        for(size_t i=0;i<cache.size();i++){
            if(cache[i].ptr==p){
                cache[i].ptr = 0;
                return;
            }
        }
    }

    ssize_t cache_search(const uint8_t *addr,size_t addrlen){
        for(size_t i = 0; i<cache.size(); i++){
            if(cache[i].ptr && memcmp(cache[i].addr,addr,addrlen)==0){
                cache_hits++;
                return i;
            }
        }
        cache_misses++;
        return -1;
    }

    void cache_replace(const uint8_t *addr,size_t addrlen,node *ptr) {
        if(++cachenext>=cache.size()) cachenext = 0;
        memcpy(cache[cachenext].addr,addr,addrlen);
        cache[cachenext].ptr = ptr;
    }


    /****************************************************************
     *** pruning
     ****************************************************************/

    /* prune the tree, starting at the root. Find the node to prune and then prune it.
     * node that best_to_prune() returns a const pointer. But we want to modify it, so we
     * do a const_cast (which is completely fine).
     */
    int prune(){
        if(root->term()) return 0;        // terminal nodes can't be pruned
        class node::best b = root->best_to_prune(root_depth);
        node *tnode = const_cast<node *>(b.best_node);
        /* remove tnode from the cache if it is present */
        if(tnode){
            return tnode->prune(*this);
        }
        return 0;
    }

    /* Simple implementation to prune the table to 90% of limit if at limit. Subclass to change behavior. */
    void prune_if_greater(size_t limit){
        if(nodes>=maxnodes){
            while(nodes > maxnodes * 9 / 10){ 
                if(prune()==0) break;         
            }
        }
    }

    /****************************************************************
     *** historam support
     ****************************************************************/

    class addr_elem {
    public:
        addr_elem(const uint8_t *addr_,uint8_t depth_,int64_t count_):
            addr(),depth(depth_),count(count_){
            memcpy((void *)addr,addr_,sizeof(addr));
        }
        addr_elem() : addr(), depth(0), count(0) {
            memset((void *) addr, 0x00, sizeof(addr));
        }
        addr_elem &operator=(const addr_elem &n){
            memcpy((void *)this->addr,n.addr,sizeof(this->addr));
            this->count = n.count;
            this->depth = n.depth;
            return *this;
        }
        virtual ~addr_elem(){}
        const uint8_t addr[ADDRBYTES];         // maximum size address; v4 addresses have addr[4..15]=0
        uint8_t depth;                         // in bits; /depth
        TYPE count;
        
        bool is4() const { return isipv4(addr,ADDRBYTES);};
        std::string str() const { return ipstr(addr,ADDRBYTES,depth); }
    };

    /** get a histogram of the tree, and starting at a particular node 
     * The histogram is reported for every node that has a sum.
     * This is terminal nodes and intermediate nodes.
     * This means that there must be a way for converting TYPE(count) to a boolean.
     *
     * @param depth - tracks current depth (in bits) into address.
     * @param ptr   - the node currently being queried
     * @param histogram - where the histogram is written
     */
    typedef vector<addr_elem> histogram_t;
    void get_histogram(int depth,const uint8_t *addr,const class node *ptr,histogram_t  &histogram) const{
        if(ptr->nodesum()){
            histogram.push_back(addr_elem(addr,depth,ptr->nodesum()));
            //return;
        }
        if(depth>max_histogram_depth) return;               // can't go deeper than this now
        
        /* create address with 0 and 1 added */
        uint8_t addr0[ADDRBYTES];
        uint8_t addr1[ADDRBYTES];
        
        memset(addr0,0,sizeof(addr0)); memcpy(addr0,addr,(depth+7)/8);
        memset(addr1,0,sizeof(addr1)); memcpy(addr1,addr,(depth+7)/8); setbit(addr1,depth);
        
        if(ptr->ptr0) get_histogram(depth+1,addr0,ptr->ptr0,histogram);
        if(ptr->ptr1) get_histogram(depth+1,addr1,ptr->ptr1,histogram);
    }
        
    void get_histogram(histogram_t &histogram) const { // adds the histogram to the passed in vector
        uint8_t addr[ADDRBYTES];
        memset(addr,0,sizeof(addr));
        get_histogram(0,addr,root,histogram);
    }

    /****************************************************************
     *** output routines
     ****************************************************************/

    // returns true if addr[4..15]==0
    static std::string itos(int n){
        char buf[64];
        snprintf(buf,sizeof(buf),"%d",n);
        return std::string(buf);
    }
    static bool isipv4(const uint8_t *addr,size_t addrlen) { 
        if(addrlen==4) return true;
        for(u_int i=4;i<addrlen;i++){
            if(addr[i]!=0) return false;
        }
        return true;
    }
    static std::string ipstr(const uint8_t *addr,size_t addrlen,size_t depth){
        if(isipv4(addr,addrlen)){
            return ipv4(addr) + (depth<ipv4_bits  ? (std::string("/") + itos(depth)) : "");
        } else {
            return ipv6(addr) + (depth<ipv6_bits ? (std::string("/") + itos(depth)) : "");
        }
    }

    /* static service routines for displaying ipv4 and ipv6 addresses  */
    static std::string ipv4(const uint8_t *a){
        char buf[1024];
        snprintf(buf,sizeof(buf),"%d.%d.%d.%d",a[0],a[1],a[2],a[3]);
        return std::string(buf);
    }
    static std::string ipv6(const uint8_t *a){ 
        char buf[128];
        return std::string(inet_ntop(AF_INET6,a,buf,sizeof(buf)));
    }
    /* dump a histogram ; largely for debugging */
    std::ostream & dump(std::ostream &os,const histogram_t &histogram) const {
        os << "nodes: " << nodes << "  histogram size: " << histogram.size() << "\n";
        for(size_t i=0;i<histogram.size();i++){
            os << histogram.at(i).str() << "  count=" << histogram.at(i).count << "\n";
        }
        return os;
    }
    /* dump the tree; largely for debugging */
    std::ostream & dump(std::ostream &os) const {
        histogram_t histogram;
        get_histogram(histogram);
        dump(os,histogram);
        return os;
    }

    /* dump the stats */
    std::ostream & dump_stats(std::ostream &os) const {
        os << "cache_hits: " << cache_hits << "\n";
        os << "cache_misses: " << cache_misses << "\n";
        return os;
    }
};


/** Add 'val' to the node associated with a particular ip address.
 * @param addr - the address
 *
 * @param addrlen - the length of the address (allows mixing of IPv4 & IPv6 in the same gree
 *
 * @param val - what to add. Use "1" to tally the number of packets,
 * "bytes" to count the number of bytes associated with each IP
 * address.
 */ 
template <typename TYPE,size_t ADDRBYTES>
void iptreet<TYPE,ADDRBYTES>::add(const uint8_t *addr,size_t addrlen,TYPE val)
{
    prune_if_greater(maxnodes);
    if(addrlen > ADDRBYTES) addrlen=ADDRBYTES;

    u_int addr_bits = addrlen * 8;  // in bits

    node *ptr = root;               // start at the root
    
    /* check the cache first */
    ssize_t i = cache_search(addr,addrlen);
    if(i>=0){
        cache[i].ptr->add(val);
        return;
    }
    
    for(u_int depth=0;depth<=addr_bits;depth++){
        if(depth==addr_bits){       // reached end of address
            ptr->add(val);          // increment this node (and all of its descendants 
            cache_replace(addr,addrlen,ptr);
            return;
        }
        /* Not a terminal node, so go down a level based on the next bit,
         * extending if necessary.
         */
        if(bit(addr,depth)==0){
            if(ptr->ptr0==0){
                ptr->ptr0 = new node(ptr);
                nodes++;
                ctr_added++;
            }
            ptr = ptr->ptr0;
        } else {
            if(ptr->ptr1==0){
                ptr->ptr1 = new node(ptr); 
                nodes++;
                ctr_added++;
            }
            ptr = ptr->ptr1;
        }
    }
    assert(0);                          // should never happen
}


/* a structure for a pair of IP addresses */
class ip2tree:public iptreet<uint64_t,32> {
public:
    /* de-interleave a pair of addresses */
    static void un_pair(uint8_t *addr1,uint8_t *addr2,size_t addr12len,size_t *depth1,size_t *depth2,const uint8_t *addr,size_t addrlen,size_t depth){
        for(size_t i=0;i<addrlen*8/2;i++){
            if(iptreet<uint64_t,32>::bit(addr,i*2))   iptreet<uint64_t,32>::setbit(addr1,i);
            if(iptreet<uint64_t,32>::bit(addr,i*2+1)) iptreet<uint64_t,32>::setbit(addr2,i);
        }
        *depth1 = (depth+1)/2;
        *depth2 = (depth)/2;
    }

    ip2tree(int maxnodes_):iptreet<uint64_t,32>(maxnodes_){}
    virtual ~ip2tree(){};
    /* str requires more work */
    static std::string ip2str(const uint8_t *addr,size_t addrlen,size_t depth){
        uint8_t addr1[16];memset(addr1,0,sizeof(addr1));
        uint8_t addr2[16];memset(addr2,0,sizeof(addr2));
        size_t depth1=0,depth2=0;
        ip2tree::un_pair(addr1,addr2,sizeof(addr1),&depth1,&depth2,addr,addrlen,depth);
        return ipstr(addr1,sizeof(addr1),depth1) + " " + ipstr(addr2,sizeof(addr2),depth2);
    }

    /* 2tree needs its own dump because a different ipstr is called */
    std::ostream & dump(std::ostream &os) const {
        histogram_t histogram;
        get_histogram(histogram);
        os << "nodes: " << nodes << "  histogram size: " << histogram.size() << "\n";
        for(size_t i=0;i<histogram.size();i++){
            const addr_elem &a = histogram.at(i);
            os << ip2str(a.addr,sizeof(a.addr),a.depth) << "  count=" << histogram.at(i).count << "\n";
        }
        return os;
    }

    /* Add a pair of addresses by interleaving them */
    void add_pair(const uint8_t *addr1,const uint8_t *addr2,size_t addrlen,uint64_t val){
        uint8_t addr[32];
        memset(addr,0,sizeof(addr));
        /* Interleave on the bit by bit level */
        for(size_t i=0;i<addrlen*8;i++){
            if(iptreet<uint64_t,32>::bit(addr1,i)) iptreet<uint64_t,32>::setbit(addr,i*2);
            if(iptreet<uint64_t,32>::bit(addr2,i)) iptreet<uint64_t,32>::setbit(addr,i*2+1);
        }
        add(addr,addrlen*2,val); /* Add it */
    }

};

typedef iptreet<uint64_t,16> iptree;       // simple tree for counting; reimplement so val is tcount
template <typename T,size_t ADDRBYTES> std::ostream & operator <<(std::ostream &os,const iptreet<T,ADDRBYTES> &ipt) {
    return ipt.dump(os);
}

inline std::ostream & operator <<(std::ostream &os,const ip2tree &ipt) {
    return ipt.dump(os);
}


#endif
