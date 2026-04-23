#ifndef _UNIFIED_INTERCONNECT_H_
#define _UNIFIED_INTERCONNECT_H_

class UnifiedInterconnect
{
public:
    UnifiedInterconnect(){};
    ~UnifiedInterconnect(){};
    static constexpr int REQ_TXN_TYPE           = 0;
    static constexpr int REQ_SOUR_ID            = 1;
    static constexpr int REQ_DEST_ID            = 2;
    static constexpr int REQ_IS_LAST            = 3;
    static constexpr int REQ_SIZE               = 4;
    static constexpr int REQ_ACK                = 5;
    static constexpr int REQ_PORT_ID            = 6;
    static constexpr int REQ_COLL_PHASE         = 7;
    static constexpr int REQ_COLL_LEVEL         = 8;
    static constexpr int REQ_COLL_SCATTER_SIZE  = 9;
    static constexpr int REQ_COLL_SCATTER_START = 10;
    static constexpr int REQ_COLL_SCATTER_END   = 11;
    static constexpr int REQ_NB_ARGS            = 12;

    //Must < 0, to distinguish from ACK values for normal requests
    static constexpr int ACK_COLLECTIVE_ACCEPT  = -1;
    static constexpr int ACK_COLLECTIVE_REJECT  = -2;

    // Transaction types
    static constexpr int TXN_TYPE_READ          = 0;
    static constexpr int TXN_TYPE_WRITE         = 1;
    static constexpr int TXN_TYPE_REDUCE        = 2;
    static constexpr int TXN_TYPE_SCATTER       = 3;
    static constexpr int TXN_TYPE_ALLREDUCE     = 4;
};

#endif
