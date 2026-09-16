#include <iostream>
#include <cstdint>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <cstring> // memcpy
#include <cstdint>
#include <bit>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <chrono>
#include <fstream>
#include <flat_map> 

#include "itch_packet_processor.h" // rename it 
#include "nasdaq_itch50.h"

using namespace std;

constexpr size_t MAX_BUFFER_SIZE = 20; // even 50 might be okay 

#pragma pack(push,1)

struct packet_header{ // 16 bytes

  uint32_t epoch_sec  = 1782259200 ; // 24 june 2026  - 12 am.
  uint32_t epoch_nsec = 0 ; // ns or microseconds.
  uint32_t captured_pkt_len ; // size of Ethernet + IP + UDP + MOLD + ITCH
  uint32_t original_pkt_len ; 

};

struct network_header_vlan{ // 62 bytes

  // Ethernet_header :  14 bytes
  
  uint8_t dest_mac[6] ;
  uint8_t source_mac[6]  ;
  uint32_t vlan_bytes = 0x8100 ; // 4 bytes , if it is there. the data will be 0x8100
  uint16_t EtherType ;  
  
  // IP :  20 bytes
  
  uint8_t  version_header ; // IPv4
  uint8_t  types_of_service ;
  uint16_t ip_length ; 
  uint16_t Identification ; 
  uint16_t flags_fragmentation  ;
  uint8_t  time_to_live ; 
  uint8_t  protocol ; 
  uint16_t ip_check_sum ; 
  uint32_t source_ip  ; 
  uint32_t dest_ip  ;
  
  // UDP :  8 bytes
  
  uint16_t source_port  ;
  uint16_t dest_port ;
  uint16_t udp_length ;
  uint16_t udp_check_sum ;
  
  // MOLD : 20 bytes
  
  char session_id[10] ;
  uint64_t sequence_number ;
  uint16_t message_count  ;
  
} ;

#pragma pack(pop)


// debug mode
bool verbose = false ; 

// parser decode mode
DecodeMode Mode = DecodeMode::Direct ;


int main(int argc , char* argv[]){

  if (argc < 2){
    std::cerr << "Usage: pcap_decoder <pcap_file>\n";
    return 1 ;
  }

  const char* input = argv[1] ;  
  
  for (int i=0 ; i < argc ; i++){
    if (std::string(argv[i]) == "--verbose"){
      verbose = true ;
    }
    if (std::string(argv[i]) == "--market-state"){
      Mode = DecodeMode::MarketState ;
    }
  }

  
  
  // ---------------------------------- //put it in some class
  
  auto fd = open(input , O_RDONLY) ;
  if (fd == -1){
    cerr << "Error opening file (pcap) " << std::endl ;
    return 1;
  }
  struct stat sb ;
  if (fstat(fd,&sb) == -1){
    cerr << "Error fstat" << std::endl ;
    return 1;
  }
  size_t file_size = sb.st_size ;
  
  void* in_pcap = mmap(nullptr, file_size , PROT_READ , MAP_PRIVATE , fd , 0);
  if (in_pcap == MAP_FAILED){
    cerr << "Error mapping file (pcap)" << std::endl ;
    return 1;
  }
  
  // ---------------------------------- //
   
  char* pcap_ptr = static_cast<char*>(in_pcap) ;
  char* pcap_end = pcap_ptr + file_size ;
  bool need_swap ; 
  
  // check endianess
  if (std::endian::native == std::endian::little) // system have same endianess header.
  {
    need_swap = false ;
  } 
  else{
    need_swap = true ;
  }
  
  // skip past global pcap header (24 bytes)
  pcap_ptr += 24 ;
  auto& ptr = pcap_ptr ; // ref
  
  // parser state 
  ParserState ps ;
  #if defined(MEASURE_RECON_LATENCY) || defined(MEASURE_PARSER_LATENCY)
  ps.message_latency_ns.reserve(50000000); // rough estimate number , for initial benchmark
  #endif
  
  // set flag
  ps.verbose = verbose ;
  ps.Mode = Mode ;
  
  auto& buffer = ps.out_of_order_buffer ; 
  
  // Ethernet length : with vlan - 18 byte . general 14 bytes
  uint8_t eth_len = (ptr[16+12] == 0x81 && ptr[16+12+1] == 0x00) ? 18 : 14 ; // 4 byte vlan if present.
  
  auto& expected_seq = ps.expected_seq ;
  
  auto start = std::chrono::high_resolution_clock::now() ;
  
  while (ptr + sizeof(packet_header) <= pcap_end){
  
    packet_header pkt ;
    std::memcpy(&pkt , ptr , sizeof(pkt)) ; 
    uint32_t cap_pkt_len ;
    
    if (need_swap) // replace with cpp23 feature checking
    {
      cap_pkt_len = std::byteswap(pkt.captured_pkt_len) ;
    }
    else{
      cap_pkt_len = pkt.captured_pkt_len ; // no swap required
    }
    
    
    // packet sequence number :
    auto seq_offset = 16 + eth_len + 20 + 8 + 10 ;
    uint64_t seq_num ;
    std::memcpy(&seq_num , ptr+seq_offset , sizeof(seq_num)) ;
    // swap
    seq_num = std::byteswap(seq_num) ;
    
    // session id :
    char10_byte session_id ;
    std::memcpy(&session_id.data , ptr+seq_offset-10 , 10) ;
  
    char10_byte curr_pkt_session_id = session_id ;
    
    // session mismatch
    if (curr_pkt_session_id != ps.prev_pkt_session_id ){
      
      // check current in dead session 
      if (curr_pkt_session_id.present_in(ps.dead_sessions) ){
        // skip this pkt. old dead session.
        ptr += (16 + cap_pkt_len) ;
        continue ;
      }
      // reset session to new session.
      else 
      { // push previous session in dead
        ps.dead_sessions.push_back(ps.prev_pkt_session_id) ; 
        
        // process previous session's buffer completely.
        while (!buffer.empty()){
          auto it = buffer.begin() ;
          // process front packet 
          process_packet(it->second, ps) ;
          // remove entry
          buffer.erase(it) ; 
        } // clean buffer.
        
        // update session
        ps.prev_pkt_session_id = curr_pkt_session_id ;  
        // reset for new session
        expected_seq = 1 ; 
        // this pkt will be processed below.
      }
    
    }
    
    
    // curr_pkt_session == ps.session OR if coming from new session (expected_seq = 1 ) 
    
    if (seq_num == expected_seq){
      // process packet
      auto offset = process_packet(ptr, ps) ; 
      ptr += offset ; // 16 + cap_pkt_len
      
      // increment exp seq
      expected_seq++ ;
      
      // process all consecutive packets if are in buffer
      while (buffer.find(expected_seq) != buffer.end() ) // if it is in buffer . process it here directly. 
      {
        process_packet(buffer[expected_seq], ps) ; // dont store offset . we doing it back of file. ptr is forward then this . as buffer already contained this expected one.
        
        buffer.erase(expected_seq) ; // clear seq number from buffer.
        expected_seq++; // = ps.next_avaiable_expected_seq(expected_seq) ; // update to non corrupt num
      }
      continue ; // to while loop , might find it forward in file.
  
    }
    
    else if (seq_num > expected_seq){ // 
    
      if (buffer.size() < MAX_BUFFER_SIZE){ // we can still afford to buffer pkts , before we give up on expected_seq pkt.
        // push curr pkt 
        buffer[seq_num] = ptr; // push current pkt in buffer. (look if other is possible ,. new.
        ptr += (16 + cap_pkt_len ) ; // update for next iteration.
        continue ; // jump next
      }
      
      // cannot buffer more.
      else if (buffer.size() == MAX_BUFFER_SIZE){ // hopes crushed. , throw away current expected_seq . update it to next.
         
         // increment
         ps.out_of_order_drops += 1 ;
         
         // skip past all numbers directly to front of buffer
         auto it = buffer.begin() ;
         process_packet(it->second , ps) ;
         buffer.erase(it);
         //push current packet now 
         buffer[seq_num] = ptr ; 
         //increment expected seq number 
         expected_seq++ ; 
         
        // process expected_seq , until it is not in buffer
        while (buffer.find(expected_seq) != buffer.end() ) // if is in buffer . process it here directly. 
        {
          process_packet(buffer[expected_seq], ps) ;
          buffer.erase(expected_seq) ; // clear seq num from buffer.
          expected_seq++;
        }
        ptr += (16 + cap_pkt_len) ;
        // expected is not in buffer.
        continue ; // go back to normal while loop , if we find it next in file , if not the same buffer full.
      }
    
    }
    
    else { // seq_num < expected_num . old out of order ghosts coming late.
    
      // increment
      ps.out_of_order_drops += 1 ;
      ptr += (16 + cap_pkt_len) ; // skip this , dead session pkt or very old pkt (same session). (old pkt means he missed the buffer window oppor to be discoverd in order.
      continue ;
    }
  
    // log progress : 
    //if(ps.total_packets%100 == 0){
    //}
  
  }
  
  auto end = std::chrono::high_resolution_clock::now() ;

  std::chrono::duration<double> d = end - start ;
  
  if (d.count() > 0.0){
    std::cout << "Packets/sec : " << ( ps.total_packets / d.count() ) << std::endl ;
    std::cout << "itch Messages/sec : " << ( ps.total_messages / d.count() ) << std::endl ;
  }
  else {
    std::cout << "d is 0" << std::endl ;
  }
  
  ps.print() ; 
  
  #if defined(MEASURE_RECON_LATENCY) || defined(MEASURE_PARSER_LATENCY)
  
  auto& lat = ps.message_latency_ns;

  std::sort(lat.begin(), lat.end());

  size_t n = lat.size();

  std::cout << "Latency p50 : "
            << lat[n * 50 / 100] << " ns\n";

  std::cout << "Latency p95 : "
            << lat[n * 95 / 100] << " ns\n";

  std::cout << "Latency p99 : "
            << lat[n * 99 / 100] << " ns\n";

  std::cout << "Latency max : "
            << lat.back() << " ns\n";
  
  #endif
  
  if (munmap(in_pcap , file_size) == -1){
    cerr << "Error unmapping" << endl;
  }
  close(fd);

  return 0 ;

}
