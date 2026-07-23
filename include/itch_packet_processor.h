#pragma once

#include <cstdint>
#include <cstring> // for memcmp 
#include "nasdaq_itch50.h"
#include "base_lob_engine.h" //
#include <bit>
#include <iostream>

using namespace std;

void read_data(void* var, void* src_ptr , size_t s){


}

template <typename T>
void log_msg(uint64_t count , T& msg , bool verbose){
    if (verbose){
      if (count == 1){
        msg.print() ;
      }
    }
}



#pragma pack(push , 1)

struct char10_byte{
  char data[10] = {0} ;
  
  // default gives != also .
  bool operator==(const char10_byte& b) const = default ;
  
    //return ( std::memcmp(data, b.data , sizeof(data)) == 0 ) ;
    
  // check id in dead session ids
  bool present_in(std::vector<char10_byte>& dead_ids) const{
    
    for(auto &id: dead_ids){
      if ( *this == id ) return true ;
    }
    
    return false ;
  }
  
} ;


struct ParserState {
  // engine
  Engine<EngineMode::Parser> engine ;
  // expected pkt seq number
  uint64_t expected_seq = 1 ; 
  // last pkt session id
  char10_byte prev_pkt_session_id ; 
  
  std::vector<char10_byte> dead_sessions ;
  
  // stores sequence number : pointer to packet header
  std::flat_map<uint64_t , char*> out_of_order_buffer ; 
  
  // debug mode flag
  bool verbose = false ;
  
  uint64_t total_packets = 0 ;
  uint64_t total_messages = 0 ;
  uint64_t adds = 0 ;
  uint64_t executes = 0 ;
  uint64_t cancels = 0 ;
  uint64_t deletes = 0 ;
  uint64_t replaces = 0 ;
  uint64_t out_of_order_drops = 0 ;
  
  void print(){
    std::cout << "total packets    : " << total_packets << std::endl ;
    std::cout << "total messages   : " << total_messages << std::endl ;
    std::cout << "total addition   : " << adds << std::endl ;
    std::cout << "total executions : " << executes << std::endl ;
    std::cout << "total cancels    : " << cancels << std::endl ;
    std::cout << "total deletions  : " << deletes << std::endl ;
    std::cout << "total replace    : " << replaces << std::endl ;
    std::cout << "out of order drops : " << out_of_order_drops << std::endl ;
  }
  
} ;

#pragma pack(pop)

  // not required , as hardware already checks corrrupt pkts and doesnt write it in pcap file. ()
  // type<seq_num> corrupt_pkts ; // store last 10 corrupt pkts , so to maintain balance of buffering when we know corrup pkt will never come in future. it was skipped already .// maybe a set.
  
  
  
  //bool is_expected_seq_corrupt(const uint64_t& expected_seq){

  //  if( corrupt_pkts.find(expected_seq) != corrupt_pkts.end() ) // if corrupt_pkts contains it . means corrupt seq num . 
  //  {
  //    return true ; // seq num is marked corrupt.
  //  }
  //  return false ;
  //}
  
  
  //uint64_t next_avaiable_expected_seq(const uint64_t& expected_seq){ // 
    
  //  uint64_t expected = 1 + expected_seq ; // new next avaiable
  //  while( is_expected_seq_corrupt(expected) ){
  //      expected++; // increment and check again until right one
  //   }
  //  return expected ;   
  //}


uint64_t process_packet(void* pkt_ptr , ParserState& ps){
    
    // parser engine
    auto& parser_engine = ps.engine ;
    
    // ptr to pkt header
    char* ptr = static_cast<char*>(pkt_ptr) ;
    
    // packet length
    uint32_t pkt_len ;
    std::memcpy(&pkt_len , ptr+8, sizeof(uint32_t)) ;
    // pkt_len = std::byteswap(pkt_len) ;
    
    // count of itch msgs in packet 
    uint16_t msg_count ;
    // itch message length & type
    uint16_t msg_len ;
    uint8_t msg_type ;
    
    // Ethernet length : with vlan - 18 byte . general 14 bytes
    uint8_t eth_len = (ptr[16+12] == 0x81 && ptr[16+12+1] == 0x00) ? 18 : 14 ; 
    
    // for msg count
    auto count_offset = 16 + eth_len+ 20 + 8 + 18 ;

    ptr += count_offset ;
    std::memcpy( &msg_count, ptr , 2) ;
    
    msg_count = std::byteswap(msg_count) ;
    ptr += 2;
    
    while (msg_count != 0 ){
      // message length
      std::memcpy(&msg_len, ptr , 2);
      msg_len = std::byteswap(msg_len);
      
      // move to message      
      ptr += 2 ; 
      
      // increment count
      ps.total_messages++ ;
      
      // message type
      memcpy(&msg_type , ptr, 1);
      
      // 
      int var = 1 ; 
      
      switch(msg_type){
      

        case 'R': // stock directory
          
          // 
          StockDirectory msg;
          std::memcpy(&msg , ptr , sizeof(StockDirectory)) ;
          
          // swap in place (register)
          msg.stock_locate = std::byteswap(msg.stock_locate);
          
          //uint64_t stock ;
          //std::memcpy(&stock , &msg.stock , sizeof(stock)) ;
          //stock = std::byteswap(stock) ;
          
          
          std::memcpy(&parser_engine.books[msg.stock_locate].stock , &msg.stock , 8  ) ;
          
          // call some function to process 
          log_msg<StockDirectory>(var,  msg, ps.verbose);
          var++ ;
          
          ptr += msg_len ;
          msg_count-- ;
          continue ;
          
           // some logic to store the stock directory : tracking num.
        
        case 'A': { // Add Order - No MPID Attribution
          
          AddOrderMessage msg ;
          std::memcpy(&msg , ptr , sizeof(AddOrderMessage)) ;
          
          msg.stock_locate = std::byteswap(msg.stock_locate) ;
          msg.order_reference_number = std::byteswap(msg.order_reference_number) ;
          msg.price = std::byteswap(msg.price) ;
          msg.shares = std::byteswap(msg.shares) ;
          char side = msg.buy_sell_indicator ;
          
          parser_engine.itch_add_order(msg.stock_locate, msg.order_reference_number , msg.price, msg.shares, side);
          ptr += msg_len ;
          msg_count-- ;
          
          // increment count
          ps.adds++ ;
          
          log_msg<AddOrderMessage>(ps.adds, msg, ps.verbose);
          continue ; 
        }
        
        case 'F': { // Add Order with MPID Attribution
          
          AddOrderAttributedMessage msg ;
          std::memcpy(&msg , ptr , sizeof(AddOrderAttributedMessage)) ;
          
          msg.stock_locate = std::byteswap(msg.stock_locate) ;
          msg.order_reference_number = std::byteswap(msg.order_reference_number) ;
          msg.price = std::byteswap(msg.price) ;
          msg.shares = std::byteswap(msg.shares) ;
          char side = msg.buy_sell_indicator ;
          
          parser_engine.itch_add_order( msg.stock_locate , msg.order_reference_number , msg.price, msg.shares , side);
          ptr += msg_len ;
          msg_count-- ;
          
          // increment count
          ps.adds++ ;
          
          log_msg<AddOrderAttributedMessage>(ps.adds, msg, ps.verbose);
          continue ;
        }
        
        case 'E': { // Order ExecutedMessage
        
          
          OrderExecutedMessage msg ;
          std::memcpy(&msg , ptr , sizeof(OrderExecutedMessage)) ;
          
          
          msg.stock_locate = std::byteswap(msg.stock_locate) ;
          msg.order_reference_number = std::byteswap(msg.order_reference_number) ;
          msg.executed_shares = std::byteswap(msg.executed_shares) ;
          
          parser_engine.itch_execute_order(msg.stock_locate , msg.order_reference_number , msg.executed_shares);
          ptr += msg_len ;
          msg_count-- ;
          
          // increment count
          ps.executes++ ;
          
          log_msg<OrderExecutedMessage>(ps.executes, msg, ps.verbose);
          continue ;
       }
        
        case 'C': { // Order Executed With PriceMessage
        
          ptr += msg_len ;
          msg_count-- ;
          continue ; // skip for now 
        }
        
        case 'X': { // Order Cancel Message
        
          OrderCancelMessage msg ;
          std::memcpy(&msg , ptr , sizeof(OrderCancelMessage)) ;
          
          msg.stock_locate = std::byteswap(msg.stock_locate) ;
          msg.order_reference_number = std::byteswap(msg.order_reference_number) ;
          msg.cancelled_shares = std::byteswap(msg.cancelled_shares) ;
          
          parser_engine.itch_reduce_order(msg.stock_locate, msg.order_reference_number , msg.cancelled_shares);
          ptr += msg_len ;
          msg_count-- ;
          
          // increment count
          ps.cancels++ ;
          
          log_msg<OrderCancelMessage>(ps.cancels, msg, ps.verbose);
          continue ;
        }
        
        case 'D': { // Order Delete Message
        
          OrderDeleteMessage msg ;
          std::memcpy(&msg , ptr , sizeof(OrderDeleteMessage)) ;
          
          msg.stock_locate = std::byteswap(msg.stock_locate) ;
          msg.order_reference_number = std::byteswap(msg.order_reference_number) ;
          
          parser_engine.itch_delete_order(msg.stock_locate, msg.order_reference_number);
          ptr += msg_len ;
          msg_count-- ;
          
          // increment count
          ps.deletes++ ;
          
          log_msg<OrderDeleteMessage>(ps.deletes, msg, ps.verbose);
          continue ;
       }
        
        case 'U': { // Order Replace Message
        
          OrderReplaceMessage msg ;
          std::memcpy(&msg , ptr , sizeof(OrderReplaceMessage)) ;
          // swap data only when required . in register direcly ( new var)
          
          msg.stock_locate = std::byteswap(msg.stock_locate) ;
          msg.original_order_reference_number = std::byteswap(msg.original_order_reference_number) ;
          msg.new_order_reference_number = std::byteswap(msg.new_order_reference_number) ;
          msg.price = std::byteswap(msg.price) ;
          msg.shares = std::byteswap(msg.shares) ;
          
          parser_engine.itch_replace_order(msg.stock_locate , msg.original_order_reference_number , msg.new_order_reference_number , msg.price , msg.shares );
          ptr += msg_len ;
          msg_count-- ;
          
          // increment count
          ps.replaces++ ;
          
          log_msg<OrderReplaceMessage>(ps.replaces, msg, ps.verbose);
          continue ;
        }
        
        default : 
        
          // std::cout << msg_type << std::endl ;
        
          ptr += msg_len ; // skip
          msg_count-- ;
          continue ;
        
        //case 'S': // system event 
        
        //case 'P': // Trade Message (Non---Cross)
        
        //case 'Q': // Cross Trade Message
        
        //case 'B': // Broken Trade / Order ExecutionMessage
        
        //case 'H': // stock trading action
        
        //case 'Y': // Reg SHO Short Sale Price Test RestrictedIndicator
        
        //case 'L': //  Market Participant Position
        
        //case 'V': // MWCB Decline Level Message
        
        //case 'W': // MWCB Status Message
      
        //case 'K': // Quoting Period Update 
        
        //case 'J': //  Limit Up - Limit Down (LULD) Auction Collar
        
        //case 'h': // Operational Halt
        
        //case 'I': // Net Order Imbalance Indicator (NOII)Message
        
        //case 'O': // Direct Listing with Capital Raise Price Discovery Message
        
        
      
      }
      
    
    }
    
  ps.total_packets +=1 ;
    
  return (16 + pkt_len) ;
}
