#include <fstream>
#include <cstdint>
#include <iostream>

#include <fcntl.h>      // For open()
#include <unistd.h>     // For close()
#include <sys/mman.h>   // For mmap() and munmap()

#include <netinet/in.h> // read.

#include <cstring> // check if this is for memcpy
#include <sys/stat.h>
#include <bit>
#include <random>

using namespace std;

#pragma pack(push, 1) // no padding

struct global_pcap{ // 24 bytes

  uint32_t magic_num = 0xA1B23C4D ; // write normal , so lsb D4 comes first in file. little endian
  uint16_t major_ver = 0x0002 ;
  uint16_t minor_ver = 0x0004 ;
  uint32_t time_offset = 0x00000000 ;
  uint32_t timestamp_accuracy = 0x00000000 ;
  uint32_t snap_len   = 0x0000FFFF ;
  uint32_t link_layer = 0x00000001 ;
} ;

struct packet_header{ // 16 bytes

  uint32_t epoch_sec  = 1782259200 ; // 24 june 2026  - 12 am.
  uint32_t epoch_nsec = 0 ; // nsecs or microseconds.
  uint32_t captured_pkt_len = 0 ; // size of Ethernet + IP + UDP + MOLD + ITCH
  uint32_t original_pkt_len = 0 ; 

};

struct network_header{ // 62 bytes

  // Ethernet_header :  14 bytes
  uint8_t dest_mac[6] = {0} ;
  uint8_t source_mac[6] = {0} ;
  // vlan 4 bytes: data will be 0x8100
  uint16_t EtherType = htons(0x0800) ;   // old comment //0080 write it fliped , so it is stored as big endian. 
  
  // IP :  20 bytes
  uint8_t  version_header = 0x45 ; // IPv4
  uint8_t  types_of_service = 0x00;
  uint16_t ip_length = 0; // complete IP + UDP + MOLD + ITCH
  uint16_t Identification = 0; // unique ip packet identifier.
  uint16_t flags_fragmentation = htons(0x4000) ;
  uint8_t  time_to_live = 0x40; // hardcoded
  uint8_t  protocol = 0x11 ; // 17 in decimal / UDP
  uint16_t ip_check_sum = 0 ; // leave for now
  uint32_t source_ip = htonl(0xC0A80105) ; // 192.168.1.5  
  uint32_t dest_ip   = htonl(0xE9360C01) ; // 233.54.12.1
  
  // UDP :  8 bytes
  uint16_t source_port = 0 ;
  uint16_t dest_port = 0 ;
  uint16_t udp_length = 0 ;
  uint16_t udp_check_sum = 0 ;
  
  // MOLD : 20 bytes
  char session_id[10] = {'N' , 'A' , 'S' , 'D' , 'A' , 'Q', '1', ' ' , ' ' , ' ' } ;
  uint64_t sequence_number = 0 ;
  uint16_t message_count = 0 ;
  
} ;


#pragma pack(pop) // Restore default compiler alignment settings


// struct __attribute__((packed)) struct_name{
//}; 

uint64_t seq_num = 1 ; // global 
uint64_t total_packets = 0 ; // count progress
uint64_t total_itch = 0 ;



size_t max_packets = 5000 ; // default
constexpr size_t PCAP_GLOBAL_HEADER_SIZE = 24;
constexpr size_t PCAP_PACKET_HEADER_SIZE = 16;

size_t PACKET_SIZE = 1514 ; // MTU limit + Ethernet header(14 bytes)
// Ethernet - IP - UDP - MOLD - 2 len - ITCH

// constexpr for Markov States: Quiet , Mid , Burst
constexpr uint16_t Quiet = 0 ;
constexpr uint16_t Mid = 1 ;
constexpr uint16_t Burst = 2 ;

// A fast, high-quality 64-bit/32-bit PRNG (WyRand)
struct WyRand {
    uint64_t state;
    
    using result_type = uint32_t;
    static constexpr uint32_t min() { return 0; }
    static constexpr uint32_t max() { return 0xFFFFFFFF; }

    uint32_t operator()() {
        state += 0xa0761d6478bd642f; // Fast state transition
        // The scramble: Multiplies and XORs to ensure low bits are perfectly uniform
        __uint128_t val = (__uint128_t)state * (state ^ 0xe7037ed1a0b428db);
        return (uint32_t)(val >> 64) ^ (uint32_t)val;
    }
};

int main(int argc, char* argv[]){

  
  if (argc < 3){
    std::cerr << "Usage: pcap_generator <input_binary> <output_pcap>\n";
    return 1 ;
  }
  

  if (argc > 3){
    try {
      uint64_t value = std::stoull(argv[3]) ;
      max_packets = static_cast<size_t>(value) ;
    }
    catch( const std::exception& e){
      std::cerr << "Error: Invalid number format for max_packets.\n" ;
      return 1 ;
    }
  }

  // file names:
  const char* input = argv[1] ;
  const char* output_path = argv[2] ;
  
  const size_t max_pcap_size = 
    PCAP_GLOBAL_HEADER_SIZE +
    (max_packets * (PACKET_SIZE + PCAP_PACKET_HEADER_SIZE) ) ;
  
  // output file mmap setup : 
  auto fd1 = open( output_path , O_RDWR | O_CREAT | O_TRUNC , 0644) ;
  
  
  if(fd1 == -1){
    cerr << "error opening file" << std::endl ;
    return 1;
  }
  
  size_t size1 = max_pcap_size ;
  
  if (ftruncate(fd1, size1) == -1) {
      perror("Error resizing file");
      close(fd1);
      return 1;
  }
    
  void* o_ptr = mmap(nullptr , size1 ,  PROT_WRITE, MAP_SHARED , fd1 , 0 ) ;
  
  if(o_ptr == MAP_FAILED){
    cerr << "mmap failed : output file" << std::endl ;
    return 1;
  }
  char* out_ptr = static_cast<char*>(o_ptr) ;
  
  
  // input file mmap setup :

  auto fd = open(input , O_RDONLY);
  
  if(fd == -1){
    cerr << "file open failed" << endl ;
    return 1 ;
  }
  
  struct stat sb ;
  if(fstat(fd , &sb) == -1) { // returns 0 for success write to sb.st_size 
    cerr << "fstat failed " << endl;
    close(fd) ;
    return 1; // error
  }
  auto file_size = sb.st_size ;
  void* in_ptr = mmap(nullptr, file_size , PROT_READ , MAP_PRIVATE, fd , 0);
  
  if(in_ptr == MAP_FAILED ) {
    cerr << "mmap failed : input file" << endl;
    close(fd) ;
    return 1  ; 
  }
  
  // setup ptrs :
  char* inp_ptr = static_cast<char*>(in_ptr) ;
  char* end_ptr = inp_ptr + file_size ;
  
  
  // write global pcap header upfront :  
  global_pcap gp ;
  memcpy(out_ptr , &gp , sizeof(gp)); // done
  out_ptr += sizeof(gp) ;
 

  // Markov process Transistion matrix :
  constexpr uint16_t TM[3][3] = {
    {52428, 11796,  1312}, // Quiet state transitions: [Q->Q, Q->M, Q->B]
    {36700, 18350, 10486}, // Mid state transitions:   [M->Q, M->M, M->B]
    { 2621, 11796, 51119}  // Burst state transitions: [B->Q, B->M, B->B]
  };

  uint16_t current_state = Quiet ; // current markov state. 
  uint64_t seed = 123456789ULL;

  auto gen = WyRand{seed} ; 

  std::uniform_int_distribution<uint16_t> dist(0,65535);

  while(inp_ptr < end_ptr){
  
    if(total_packets >= max_packets){
      break ;
    }
    
    // logic for realistic packet size modellling:

    // some logic to do generaate a 64 bit number .
    // 64 bit
    uint64_t num = gen();

    // scaled probability for markov check
    uint16_t num16 = dist(gen) ;

    // define base 
    auto base = TM[current_state][Quiet] ;

    // markov state transistion
    if (num16 <= base){
      current_state = Quiet ;
    }
    else if (num16 <= base + TM[current_state][Mid]){
      current_state = Mid ;
    }
    else {
      current_state = Burst ;
    }

    // packet size updation . (new state)
    if (current_state  == Quiet){
      // use the 19th bit of num for random coin flip
      uint8_t coin = (num >> 19) & 0x1 ;
      // only two sizes here in Quite state.
      PACKET_SIZE = (coin == 0) ? 114 : 166 ;
    }
    else if (current_state == Mid){
      // use bit 16 to 18 of num
      uint8_t x = (num >> 16) & 0b0111 ; // 7
      /// update using the sizes 
      PACKET_SIZE = 218 + (x * 52) ;
    }
    else{ // burst
      // fixed to Max MTU + ethernet header (14)
      PACKET_SIZE = 1514 ;
    }

    
    // memcpy(ptr to destination , ptr to src , size to copy from soruce to destination );
    
    char buffer[PACKET_SIZE] = {}; // data packet
    int payload_offset = 62 ; // leave space for ether/ip/udp/mold (14+20+8+20)
    int total_payload_size = 0 ;
    uint16_t msg_count = 0 ;
    
    while(true){
     
      if (inp_ptr + sizeof(uint16_t) > end_ptr){
        break ;
      }
      
      uint16_t msg_len ; 
      std::memcpy(&msg_len, inp_ptr , sizeof(msg_len) ) ;
      msg_len = std::byteswap(msg_len) ; 
      
      int data_len = 2 + msg_len ;
      
      if (inp_ptr + data_len > end_ptr){
        break ;
      }
      
      if( (payload_offset + data_len) > PACKET_SIZE ){
        break ;
      }
      
      std::memcpy(&buffer[payload_offset] , inp_ptr , data_len);
      
      payload_offset += data_len ;
      inp_ptr += data_len ;
      total_payload_size += data_len ; 
      msg_count++;
     
    }
    
    if(total_payload_size == 0){
      break ;
    }
    
    // push packet header :
    packet_header pkt_header ; 
    pkt_header.captured_pkt_len = payload_offset ;
    pkt_header.original_pkt_len = payload_offset ;
    
    std::memcpy( out_ptr , &pkt_header , sizeof(pkt_header) );
    out_ptr += sizeof(pkt_header) ;
    
    
    // set network packet
    network_header net_pkt ; 
    net_pkt.sequence_number = std::byteswap(seq_num++);
    net_pkt.ip_length = std::byteswap(static_cast<uint16_t>(payload_offset - 14)) ; // exclude Ether
    net_pkt.udp_length = std::byteswap(static_cast<uint16_t>(payload_offset - 14 - 20)) ; // exclude IP also.
    net_pkt.message_count = std::byteswap(msg_count) ;
    
    // write network packet to buffer
    memcpy(buffer , &net_pkt , sizeof(net_pkt)); // network packet
    
    // write buffer to output pcap file
    memcpy(out_ptr , buffer , payload_offset);
    out_ptr += payload_offset ; 
    
    total_packets++;
    total_itch += msg_count ;
    
    if(total_packets %10 ==0){
      cout << "Total Packets Written : " << total_packets << std::endl ;
      cout << "Total itch    Written : " << total_itch << std::endl ;
    }
     
  }
     
     
  msync(o_ptr, size1, MS_SYNC); // flush to disk.
  
  size_t actual_data_size = out_ptr - static_cast<char*>(o_ptr) ; // bytes
  auto result = ftruncate(fd1, actual_data_size);
  if (result == -1 ){
    std::cerr << "failed to resize pcap at end" ;
    return -1;
  }
  
  if (munmap(o_ptr, size1) == -1) {
        cerr << "Error unmapping" << endl;
  }
  close(fd1);
  
  if (munmap(in_ptr, file_size ) == -1) {
        cerr << "Error unmapping" << endl;
  }
  close(fd);


  return 0 ;
}
