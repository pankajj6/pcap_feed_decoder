#pragma once

#include <iostream>
#include <cstdint>
#include <bit>
#include <cstring>

// itch message structs


enum class msg: uint8_t{
    A = 0 , // add 
    E = 1, // exec
    U = 2, // replace
    X = 3, // cancel
    D = 4, // delete
    P = 5, // trade (non cross)
    Q = 6  // cross
} ;

enum class cross: uint8_t{
  O = 0, // opening
  C = 1, // closing
  H = 2  // halt/paused
} ;




#pragma pack(push,1)

struct StockDirectory {
    char message_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    char stock[8];
    char market_category;
    char financial_status_indicator;
    uint32_t round_lot_size;
    char round_lots_only;
    char issue_classification;
    char issue_sub_type[2];
    char authenticity;
    char short_sale_threshold_indicator;
    char ipo_flag;
    char luld_reference_price_tier;
    char etp_flag;
    uint32_t etp_leverage_factor;
    char inverse_indicator;
    
    void print(){
      // time 
      uint64_t time_ns = 0 ;
      std::memcpy(&time_ns , &timestamp , sizeof(timestamp)) ;
      time_ns = std::byteswap(time_ns) >> 16 ;
    
      std::cout << "Stock Directory | " << "| timestamp : " << time_ns << "| stock : " ;
      for(auto& a: stock){
        std::cout << a ;
      }
      std::cout << "| locate : " << stock_locate << std::endl ;
    }
    
};


struct AddOrderMessage {
    char     message_type;           // Offset  0, Length 1: Always 'A'
    uint16_t stock_locate;           // Offset  1, Length 2: Locate code
    uint16_t tracking_number;        // Offset  3, Length 2: Internal tracking number
    uint8_t  timestamp[6];           // Offset  5, Length 6: Nanoseconds since midnight
    uint64_t order_reference_number; // Offset 11, Length 8: Unique reference number
    char     buy_sell_indicator;     // Offset 19, Length 1: 'B' = Buy, 'S' = Sell
    uint32_t shares;                 // Offset 20, Length 4: Number of shares
    char     stock[8];               // Offset 24, Length 8: Stock symbol, space-padded
    uint32_t price;                  // Offset 32, Length 4: Display price
    
    void print() {
      std::cout << "Order Add -- " << "| order id : " << order_reference_number << "| price : " << price << "| shares : " << shares << "| side: " << buy_sell_indicator << std::endl ;
    }
};


struct AddOrderAttributedMessage {
    char     message_type;           // Offset  0, Length 1: Always 'F'
    uint16_t stock_locate;           // Offset  1, Length 2: Locate code
    uint16_t tracking_number;        // Offset  3, Length 2: Internal tracking number
    uint8_t  timestamp[6];           // Offset  5, Length 6: Nanoseconds since midnight
    uint64_t order_reference_number; // Offset 11, Length 8: Unique reference number
    char     buy_sell_indicator;     // Offset 19, Length 1: 'B' = Buy, 'S' = Sell
    uint32_t shares;                 // Offset 20, Length 4: Number of shares
    char     stock[8];               // Offset 24, Length 8: Stock symbol, space-padded
    uint32_t price;                  // Offset 32, Length 4: Display price
    char     attribution[4];         // Offset 36, Length 4: MPID associated with the order
    
    void print() {
      std::cout << "Order Add (attri) -- " << "| order id : " << order_reference_number << "| price : " << price << "| shares : " << shares << "| side: " << buy_sell_indicator << std::endl ;
    }
};


struct OrderExecutedMessage {
    char     message_type;           // Offset  0, Length 1: Always 'E'
    uint16_t stock_locate;           // Offset  1, Length 2: Locate code
    uint16_t tracking_number;        // Offset  3, Length 2: Internal tracking number
    uint8_t  timestamp[6];           // Offset  5, Length 6: Nanoseconds since midnight
    uint64_t order_reference_number; // Offset 11, Length 8: Order being executed
    uint32_t executed_shares;        // Offset 19, Length 4: Number of shares executed
    uint64_t match_number;           // Offset 23, Length 8: Day-unique match number
    
    void print() {
      std::cout << "Order Execution -- " << "| order id : " << order_reference_number << "| shares : " << executed_shares << std::endl ;
    }
};


struct OrderCancelMessage {
    char     message_type;           // Offset  0, Length 1: Always 'X'
    uint16_t stock_locate;           // Offset  1, Length 2: Locate code
    uint16_t tracking_number;        // Offset  3, Length 2: Internal tracking number
    uint8_t  timestamp[6];           // Offset  5, Length 6: Nanoseconds since midnight
    uint64_t order_reference_number; // Offset 11, Length 8: Reference number of the order being canceled
    uint32_t cancelled_shares;       // Offset 19, Length 4: Number of shares being removed
    
    void print() {
      std::cout << "Order Cancel -- " << "| order id : " << order_reference_number << "| shares : " << cancelled_shares << std::endl ;
    }
};

struct OrderDeleteMessage {
    char     message_type;           // Offset  0, Length 1: Always 'D'
    uint16_t stock_locate;           // Offset  1, Length 2: Locate code
    uint16_t tracking_number;        // Offset  3, Length 2: Internal tracking number
    uint8_t  timestamp[6];           // Offset  5, Length 6: Nanoseconds since midnight
    uint64_t order_reference_number; // Offset 11, Length 8: Reference number of the order being deleted
    
    void print() {
      std::cout << "Order Delete -- " << "| order id : " << order_reference_number << std::endl ;
    }
};


struct OrderReplaceMessage {
    char     message_type;                    // Offset  0, Length 1: Always 'U'
    uint16_t stock_locate;                    // Offset  1, Length 2: Locate code
    uint16_t tracking_number;                 // Offset  3, Length 2: Internal tracking number
    uint8_t  timestamp[6];                    // Offset  5, Length 6: Nanoseconds since midnight
    uint64_t original_order_reference_number; // Offset 11, Length 8: Order being replaced
    uint64_t new_order_reference_number;      // Offset 19, Length 8: New order ID to use henceforth
    uint32_t shares;                          // Offset 27, Length 4: New total displayed quantity
    uint32_t price;                           // Offset 31, Length 4: New display price
    
    void print() {
      std::cout << "Order Replace -- " << "| org order id : " << original_order_reference_number << "| new id : " << new_order_reference_number << "| price : " << price << "| shares : " << shares << std::endl ;
    }
    
};

#pragma pack(pop)


struct Trade{ // hidden order (non displayable)

  uint32_t price ;
  uint32_t shares ;
} ;

struct CrossTrade{

  uint32_t cross_price  ;
  uint32_t shares ; 
  cross  cross_type ; // "O" , "C" , "H"
} ;



//OrderAdd o = {} ;
//*(reinterpret_cast<OrderAdd*>(ptr)) = o ;
