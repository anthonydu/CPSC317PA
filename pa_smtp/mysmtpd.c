#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "mailuser.h"
#include "netbuffer.h"
#include "server.h"
#include "util.h"

#define MAX_LINE_LENGTH 1024

typedef enum state {
  Undefined,
  idle,
  sending
  // TODO: Add additional states as necessary
} State;

typedef struct smtp_state {
  int fd;
  net_buffer_t nb;
  char recvbuf[MAX_LINE_LENGTH + 1];
  char *words[MAX_LINE_LENGTH];
  int nwords;
  State state;
  struct utsname my_uname;
  char sender[MAX_USERNAME_SIZE];
  user_list_t receivers;
  char mail_content[MAX_LINE_LENGTH * 1000];
  // TODO: Add additional fields as necessary
} smtp_state;

static void handle_client(int fd);

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
    return 1;
  }

  run_server(argv[1], handle_client);

  return 0;
}

// syntax_error returns
//   -1 if the server should exit
//    1  otherwise
int syntax_error(smtp_state *ms) {
  if (send_formatted(
          ms->fd, "501 %s\r\n", "Syntax error in parameters or arguments")
      <= 0)
    return -1;
  return 1;
}

// checkstate returns
//   -1 if the server should exit
//    0 if the server is in the appropriate state
//    1 if the server is not in the appropriate state
int checkstate(smtp_state *ms, State s) {
  // printf("ms->state: %d, s: %d\n", ms->state, s);
  if (ms->state != s) {
    if (send_formatted(ms->fd, "503 %s\r\n", "Bad sequence of commands") <= 0)
      return -1;
    return 1;
  }
  return 0;
}

// All the functions that implement a single command return
//   -1 if the server should exit
//    0 if the command was successful
//    1 if the command was unsuccessful

int do_quit(smtp_state *ms) {
  dlog("Executing quit\n");
  if (ms->nwords != 1) return syntax_error(ms);
  int status
      = send_formatted(ms->fd, "221 Service closing transmission channel\r\n");
  if (status >= 0)
    return -1;
  else
    return 1;
}

int do_helo(smtp_state *ms) {
  dlog("Executing helo\n");
  if (checkstate(ms, Undefined)) return 1;
  ms->state = idle;
  ms->receivers = user_list_create();
  if (ms->nwords != 2) return syntax_error(ms);
  int status = send_formatted(ms->fd, "250 %s\r\n", ms->my_uname.nodename);
  if (status >= 0)
    return 0;
  else
    return 1;
}

void init(smtp_state *ms) {
  user_list_destroy(ms->receivers);
  ms->receivers = user_list_create();
  ms->mail_content[0] = '\0';
  ms->state = idle;
}

int do_rset(smtp_state *ms) {
  dlog("Executing rset\n");
  // TODO: Implement this function
  if (ms->nwords != 1) return syntax_error(ms);
  init(ms);
  int status = send_formatted(ms->fd, "250 State reset\r\n");
  if (status >= 0)
    return 0;
  else
    return 1;
}

int do_mail(smtp_state *ms) {
  dlog("Executing mail\n");
  // TODO: Implement this function
  if (checkstate(ms, idle)) return 1;
  if (ms->nwords != 2) return syntax_error(ms);
  if (strncmp(ms->words[1], "FROM:<", 6) != 0) return syntax_error(ms);
  if (strncmp(ms->words[1] + strlen(ms->words[1]) - 1, ">", 1) != 0)
    return syntax_error(ms);
  strncpy(ms->sender, ms->words[1] + 6, strlen(ms->words[1]) - 7);
  ms->sender[strlen(ms->words[1]) - 7] = '\0';
  int status
      = send_formatted(ms->fd, "250 Requested mail action ok, completed\r\n");
  init(ms);
  ms->state = sending;
  if (status >= 0) {
    return 0;
  } else
    return 1;
}

int do_rcpt(smtp_state *ms) {
  dlog("Executing rcpt\n");
  // TODO: Implement this function
  if (checkstate(ms, sending)) return 1;
  if (ms->nwords != 2) return syntax_error(ms);
  if (strncmp(ms->words[1], "TO:<", 4) != 0) return syntax_error(ms);
  if (strncmp(ms->words[1] + strlen(ms->words[1]) - 1, ">", 1) != 0)
    return syntax_error(ms);
  char user[MAX_USERNAME_SIZE] = "";
  strncat(user, ms->words[1] + 4, strlen(ms->words[1]) - 5);
  if (!is_valid_user(user, NULL)) {
    send_formatted(ms->fd, "550 No such user - %s\r\n", user);
    return 1;
  }
  user_list_add(&(ms->receivers), user);
  int status
      = send_formatted(ms->fd, "250 Requested mail action ok, completed\r\n");
  if (status >= 0) {
    return 0;
  } else
    return 1;
}

int do_data(smtp_state *ms) {
  dlog("Executing data\n");
  // TODO: Implement this function
  if (checkstate(ms, sending)) return 1;
  if (user_list_len(ms->receivers) == 0) {
    checkstate(ms, idle);
    return 1;
  }
  if (ms->nwords != 1) return syntax_error(ms);
  int status;
  status = send_formatted(
      ms->fd, "354 Waiting for data, finish with <CR><LF>.<CR><LF>\r\n");
  if (status < 0) return 1;
  size_t len;
  while ((len = nb_read_line(ms->nb, ms->recvbuf)) >= 0) {
    if (strcmp(ms->recvbuf, ".\n") == 0 || strcmp(ms->recvbuf, ".\r\n") == 0)
      break;
    if (strncmp(ms->recvbuf, ".", 1) == 0)
      strncat(ms->mail_content, ms->recvbuf + 1, len - 1);
    else
      strncat(ms->mail_content, ms->recvbuf, len);
  }
  char template[] = "/tmp/fileXXXXXX";
  mkstemp(template);
  FILE *f = fopen(template, "w");
  fprintf(f, "%s", ms->mail_content);
  fclose(f);
  save_user_mail(template, ms->receivers);
  status
      = send_formatted(ms->fd, "250 Requested mail action ok, completed\r\n");
  ms->state = idle;
  if (status >= 0) {
    init(ms);
    return 0;
  } else
    return 1;
}

int do_noop(smtp_state *ms) {
  dlog("Executing noop\n");
  if (ms->nwords != 1) return syntax_error(ms);
  int status = send_formatted(ms->fd, "250 OK (noop)\r\n");
  if (status >= 0)
    return 0;
  else
    return 1;
}

int do_vrfy(smtp_state *ms) {
  dlog("Executing vrfy\n");
  if (ms->nwords != 2) return syntax_error(ms);
  char user[MAX_USERNAME_SIZE] = "";
  if (strncmp(ms->words[1], "<", 1) == 0
      && strncmp(ms->words[1] + strlen(ms->words[1]) - 1, ">", 1) == 0)
    strncat(user, ms->words[1] + 1, strlen(ms->words[1]) - 2);
  else
    strcat(user, ms->words[1]);
  int status;
  if (!is_valid_user(user, NULL)) {
    status = send_formatted(ms->fd, "550 No such user - %s\r\n", user);
    return 0;
  } else {
    status = send_formatted(ms->fd, "250 %s\r\n", ms->words[1]);
  }
  if (status >= 0)
    return 0;
  else
    return 1;
}

void handle_client(int fd) {
  size_t len;
  smtp_state mstate, *ms = &mstate;

  ms->fd = fd;
  ms->nb = nb_create(fd, MAX_LINE_LENGTH);
  ms->state = Undefined;
  user_list_destroy(ms->receivers);
  uname(&ms->my_uname);

  if (send_formatted(fd, "220 %s Service ready\r\n", ms->my_uname.nodename)
      <= 0)
    return;

  while ((len = nb_read_line(ms->nb, ms->recvbuf)) >= 0) {
    if (ms->recvbuf[len - 1] != '\n') {
      // command line is too long, stop immediately
      send_formatted(fd, "500 Syntax error, command unrecognized\r\n");
      break;
    }
    if (strlen(ms->recvbuf) < len) {
      // received null byte somewhere in the string, stop immediately.
      send_formatted(fd, "500 Syntax error, command unrecognized\r\n");
      break;
    }

    // Remove CR, LF and other space characters from end of buffer
    while (isspace(ms->recvbuf[len - 1])) ms->recvbuf[--len] = 0;

    dlog("Command is %s\n", ms->recvbuf);

    // Split the command into its component "words"
    ms->nwords = split(ms->recvbuf, ms->words);
    char *command = ms->words[0];

    if (!strcasecmp(command, "QUIT")) {
      if (do_quit(ms) == -1) break;
    } else if (!strcasecmp(command, "HELO") || !strcasecmp(command, "EHLO")) {
      if (do_helo(ms) == -1) break;
    } else if (!strcasecmp(command, "MAIL")) {
      if (do_mail(ms) == -1) break;
    } else if (!strcasecmp(command, "RCPT")) {
      if (do_rcpt(ms) == -1) break;
    } else if (!strcasecmp(command, "DATA")) {
      if (do_data(ms) == -1) break;
    } else if (!strcasecmp(command, "RSET")) {
      if (do_rset(ms) == -1) break;
    } else if (!strcasecmp(command, "NOOP")) {
      if (do_noop(ms) == -1) break;
    } else if (!strcasecmp(command, "VRFY")) {
      if (do_vrfy(ms) == -1) break;
    } else if (!strcasecmp(command, "EXPN") || !strcasecmp(command, "HELP")) {
      dlog("Command not implemented \"%s\"\n", command);
      if (send_formatted(fd, "502 Command not implemented\r\n") <= 0) break;
    } else {
      // invalid command
      dlog("Illegal command \"%s\"\n", command);
      if (send_formatted(fd, "500 Syntax error, command unrecognized\r\n") <= 0)
        break;
    }
  }

  nb_destroy(ms->nb);
}
