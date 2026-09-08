#include <stddef.h>
#include <stdint.h>

#include "app/eapp_utils.h"

#define SYS_CLOSE 57
#define SYS_SOCKET 198
#define SYS_BIND 200
#define SYS_GETSOCKNAME 204
#define SYS_SENDTO 206
#define SYS_RECVFROM 207
#define SYS_SETSOCKOPT 208

#define AF_INET 2
#define SOCK_DGRAM 2
#define SOL_SOCKET 1
#define SO_RCVTIMEO 20
#define EAFNOSUPPORT 97
#define TEST_SUCCESS 86420UL

typedef uint32_t socklen_t;

struct sockaddr_in_test {
  uint16_t family;
  uint16_t port;
  uint32_t address;
  unsigned char zero[8];
};

struct timeval_test {
  long seconds;
  long microseconds;
};

static uintptr_t syscall6(uintptr_t number, uintptr_t arg0, uintptr_t arg1,
                          uintptr_t arg2, uintptr_t arg3, uintptr_t arg4,
                          uintptr_t arg5)
{
  register uintptr_t a0 __asm__("a0") = arg0;
  register uintptr_t a1 __asm__("a1") = arg1;
  register uintptr_t a2 __asm__("a2") = arg2;
  register uintptr_t a3 __asm__("a3") = arg3;
  register uintptr_t a4 __asm__("a4") = arg4;
  register uintptr_t a5 __asm__("a5") = arg5;
  register uintptr_t a7 __asm__("a7") = number;

  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                     "r"(a7)
                   : "memory");
  return a0;
}

static uint32_t host_to_network32(uint32_t value)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap32(value);
#else
  return value;
#endif
}

static void close_if_open(int fd)
{
  if (fd >= 0)
    syscall6(SYS_CLOSE, (uintptr_t)fd, 0, 0, 0, 0, 0);
}

void EAPP_ENTRY eapp_entry(void)
{
  static const unsigned char message[] = "zion-net";
  struct timeval_test timeout = {1, 0};
  struct sockaddr_in_test bound = {0};
  struct sockaddr_in_test destination = {0};
  struct sockaddr_in_test source = {0};
  socklen_t bound_length = sizeof(bound);
  socklen_t source_length = sizeof(source);
  unsigned char received[sizeof(message) - 1] = {0};
  int receiver = -1;
  int sender = -1;
  uintptr_t result;

  if (syscall6(SYS_SOCKET, (uintptr_t)-1, SOCK_DGRAM, 0, 0, 0, 0) !=
      (uintptr_t)-EAFNOSUPPORT)
    EAPP_RETURN(1);

  receiver = (int)syscall6(SYS_SOCKET, AF_INET, SOCK_DGRAM, 0, 0, 0, 0);
  sender = (int)syscall6(SYS_SOCKET, AF_INET, SOCK_DGRAM, 0, 0, 0, 0);
  if (receiver < 0 || sender < 0)
    goto fail_socket;

  if ((intptr_t)syscall6(SYS_SETSOCKOPT, receiver, SOL_SOCKET, SO_RCVTIMEO,
                         (uintptr_t)&timeout, sizeof(timeout), 0) < 0)
    goto fail_timeout;
  if (syscall6(SYS_SETSOCKOPT, receiver, SOL_SOCKET, SO_RCVTIMEO,
               (uintptr_t)&timeout, 257, 0) != (uintptr_t)-1)
    goto fail_bounds;

  bound.family = AF_INET;
  if (syscall6(SYS_BIND, receiver, (uintptr_t)&bound, 129, 0, 0, 0) !=
      (uintptr_t)-1)
    goto fail_bounds;
  if ((intptr_t)syscall6(SYS_BIND, receiver, (uintptr_t)&bound,
                         sizeof(bound), 0, 0, 0) < 0)
    goto fail_bind;

  bound_length = 129;
  if (syscall6(SYS_GETSOCKNAME, receiver, (uintptr_t)&bound,
               (uintptr_t)&bound_length, 0, 0, 0) != (uintptr_t)-1)
    goto fail_bounds;
  bound_length = sizeof(bound);
  if ((intptr_t)syscall6(SYS_GETSOCKNAME, receiver, (uintptr_t)&bound,
                         (uintptr_t)&bound_length, 0, 0, 0) < 0 ||
      bound_length < sizeof(bound) || bound.family != AF_INET ||
      bound.port == 0)
    goto fail_name;

  destination.family = AF_INET;
  destination.port = bound.port;
  destination.address = host_to_network32(0x7f000001U);

  if (syscall6(SYS_SENDTO, sender, UINTPTR_MAX - 3, sizeof(message) - 1, 0,
               (uintptr_t)&destination, sizeof(destination)) !=
      (uintptr_t)-1 ||
      syscall6(SYS_SENDTO, sender, (uintptr_t)message, sizeof(message) - 1,
               0, (uintptr_t)&destination, (uintptr_t)-1) !=
          (uintptr_t)-1)
    goto fail_bounds;

  result = syscall6(SYS_SENDTO, sender, (uintptr_t)message,
                    sizeof(message) - 1, 0, (uintptr_t)&destination,
                    sizeof(destination));
  if (result != sizeof(message) - 1)
    goto fail_send;

  source_length = 129;
  if (syscall6(SYS_RECVFROM, receiver, (uintptr_t)received,
               sizeof(received), 0, (uintptr_t)&source,
               (uintptr_t)&source_length) != (uintptr_t)-1)
    goto fail_bounds;
  source_length = sizeof(source);
  result = syscall6(SYS_RECVFROM, receiver, (uintptr_t)received,
                    sizeof(received), 0, (uintptr_t)&source,
                    (uintptr_t)&source_length);
  if (result != sizeof(received) || source_length < sizeof(source) ||
      source.family != AF_INET)
    goto fail_receive;

  for (size_t i = 0; i < sizeof(received); ++i) {
    if (received[i] != message[i])
      goto fail_payload;
  }

  if ((intptr_t)syscall6(SYS_CLOSE, receiver, 0, 0, 0, 0, 0) < 0 ||
      (intptr_t)syscall6(SYS_CLOSE, sender, 0, 0, 0, 0, 0) < 0)
    EAPP_RETURN(10);
  EAPP_RETURN(TEST_SUCCESS);

fail_payload:
  close_if_open(receiver);
  close_if_open(sender);
  EAPP_RETURN(9);
fail_receive:
  close_if_open(receiver);
  close_if_open(sender);
  EAPP_RETURN(8);
fail_send:
  close_if_open(receiver);
  close_if_open(sender);
  EAPP_RETURN(7);
fail_name:
  close_if_open(receiver);
  close_if_open(sender);
  EAPP_RETURN(6);
fail_bind:
  close_if_open(receiver);
  close_if_open(sender);
  EAPP_RETURN(5);
fail_bounds:
  close_if_open(receiver);
  close_if_open(sender);
  EAPP_RETURN(4);
fail_timeout:
  close_if_open(receiver);
  close_if_open(sender);
  EAPP_RETURN(3);
fail_socket:
  close_if_open(receiver);
  close_if_open(sender);
  EAPP_RETURN(2);
}
