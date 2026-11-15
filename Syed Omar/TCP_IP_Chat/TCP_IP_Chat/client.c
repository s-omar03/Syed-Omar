/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : client.c
 *           Descriptions   : Chat client. One poll() loop watches the keyboard, the server
 *                            socket and the signal pipe; a state machine decides what the
 *                            next input line means and what each server packet answers.
 *           Objective      : Register / log in, show the online users, single chat, group
 *                            chat and logout (spec 2.2, 2.3), while messages from other users
 *                            are printed as soon as they arrive
 *           Usage          : ./client [server_ip] [port]      (defaults: 127.0.0.1 6333)
 *           Output         : Menus, prompts, server replies and incoming messages on stdout
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* clock_gettime() */

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "client_ui.h"
#include "protocol.h"
#include "signal_pipe.h"
#include "tcp.h"

#define LOGOUT_TIMEOUT_MS   3000        /* wait this long for the logout reply at exit */
#define RECV_TIMEOUT_SEC    10          /* give up on a packet that stops half-way     */
#define MS_PER_SECOND       1000
#define NS_PER_MS           1000000L
#define NS_PER_SECOND       1000000000L

#define POLL_SOCKET         0
#define POLL_SIGNAL         1
#define POLL_STDIN          2
#define POLL_COUNT          3

#define ASCII_DEL           '\177'

#define BACK_COMMAND        "/back"
#define AUTH_MENU_TEXT      "Enter option : 1 ) Register\n2 ) Login\n3 ) Exit\n"
#define CHAT_MENU_TEXT      "Select\n1 ) Single Chat\n2 ) Group Chat\n3 ) Logout\n4 ) Online Users\n"
#define CHOICE_REGISTER     "1"         /* start menu */
#define CHOICE_LOGIN        "2"
#define CHOICE_EXIT         "3"
#define CHOICE_SINGLE_CHAT  "1"         /* main menu  */
#define CHOICE_GROUP_CHAT   "2"
#define CHOICE_LOGOUT       "3"
#define CHOICE_ONLINE_USERS "4"
#define PROMPT_USERNAME     "Enter username : "
#define PROMPT_PASSWORD     "Enter password : "
#define PROMPT_RECEIVER     "Enter receiver username : "
#define PROMPT_MESSAGE      "Enter the message to send : "
#define TEXT_BAD_USERNAME   "Invalid username: use 1 to 31 letters, digits, '_', '.' or '-'"
#define TEXT_BAD_PASSWORD   "Invalid password: use 1 to 31 printable characters without spaces"

/* What the client is doing; only the input states read from stdin */
typedef enum
{
    ST_WAIT_WELCOME,        /* connected, waiting for the server's welcome  */
    ST_AUTH_MENU,           /* input: 1 register, 2 login, 3 exit           */
    ST_USERNAME,            /* input: user name                             */
    ST_PASSWORD,            /* input: password (echo off on a terminal)     */
    ST_WAIT_AUTH,           /* waiting for the register / login reply       */
    ST_WAIT_LIST,           /* waiting for the list of online users         */
    ST_MENU,                /* input: 1 single, 2 group, 3 logout, 4 list   */
    ST_RECEIVER,            /* input: receiver of a single chat             */
    ST_SINGLE_CHAT,         /* input: private messages until /back          */
    ST_GROUP_CHAT,          /* input: group messages until /back            */
    ST_WAIT_SEND,           /* waiting for the reply to a chat message      */
    ST_WAIT_LOGOUT,         /* waiting for the logout reply                 */
    ST_DONE                 /* leave the main loop                          */
} ClientState;

typedef struct
{
    int sock;                           /* connection to the server            */
    int signal_fd;                      /* read end of the signal pipe         */
    int stdin_tty;                      /* stdin is a terminal                 */
    ClientState state;
    ClientState chat_state;             /* chat state to return to after a send */
    uint32_t auth_option;               /* AUTH_REGISTER or AUTH_LOGIN         */
    int logged_in;
    char username[PROTO_NAME_SIZE];     /* user being authenticated / logged in */
    char receiver[PROTO_NAME_SIZE];     /* single chat partner                 */
    int exit_requested;                 /* EOF or signal: log out, then exit   */
    struct timespec exit_deadline;      /* give up waiting for the server then */
    int exit_status;
    LineReader input;
} Client;

/*
 * Function    : print_usage
 * Description : Prints the command line summary
 */
static void print_usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [server_ip] [port]\n"
            "  server_ip   address or host name of the chat server (default %s)\n"
            "  port        TCP port of the chat server (default %d)\n",
            prog, CHAT_DEFAULT_SERVER_IP, CHAT_DEFAULT_PORT);
}

/*
 * Function    : state_reads_input
 * Description : True for the states that take a line from stdin
 */
static int state_reads_input(ClientState state)
{
    return state == ST_AUTH_MENU || state == ST_USERNAME || state == ST_PASSWORD
           || state == ST_MENU || state == ST_RECEIVER || state == ST_SINGLE_CHAT
           || state == ST_GROUP_CHAT;
}

/*
 * Function    : state_prompt
 * Description : The same-line prompt of an input state (NULL for menus and
 *               waiting states)
 */
static const char *state_prompt(ClientState state)
{
    switch (state)
    {
    case ST_USERNAME:
        return PROMPT_USERNAME;
    case ST_PASSWORD:
        return PROMPT_PASSWORD;
    case ST_RECEIVER:
        return PROMPT_RECEIVER;
    case ST_SINGLE_CHAT:
    case ST_GROUP_CHAT:
        return PROMPT_MESSAGE;
    default:
        return NULL;
    }
}

/*
 * Function    : enter_state
 * Description : Switches state and shows the menu or prompt of the new
 *               state. Terminal echo is off exactly while in ST_PASSWORD.
 */
static void enter_state(Client *c, ClientState next)
{
    const char *prompt = state_prompt(next);

    if (c->state == ST_PASSWORD && next != ST_PASSWORD)
        ui_echo_restore();
    c->state = next;
    if (next == ST_AUTH_MENU)
        fputs(AUTH_MENU_TEXT, stdout);
    else if (next == ST_MENU)
        fputs(CHAT_MENU_TEXT, stdout);
    else if (next == ST_PASSWORD && ui_echo_off(STDIN_FILENO) != 0)
        puts("(Warning: could not hide the password)");
    if (prompt != NULL)
        ui_show_prompt(prompt);
}

/*
 * Function    : finish
 * Description : Ends the main loop with the given exit status
 */
static void finish(Client *c, int status)
{
    ui_echo_restore();
    c->state = ST_DONE;
    c->exit_status = status;
}

/*
 * Function    : lost_connection
 * Description : Sending to the server failed
 */
static void lost_connection(Client *c)
{
    puts("Lost the connection to the server");
    finish(c, EXIT_FAILURE);
}

/*
 * Function    : send_logout
 * Description : Sends the logout request (spec 3.5: a chat message with
 *               option 3) and waits for the reply
 */
static void send_logout(Client *c)
{
    if (proto_send_chat(c->sock, c->username, "", "", CHAT_LOGOUT) != PROTO_OK)
    {
        lost_connection(c);
        return;
    }
    enter_state(c, ST_WAIT_LOGOUT);
}

/*
 * Function    : resume
 * Description : A wait is over: continue in 'next', or carry on with a
 *               requested exit (log out first when logged in)
 */
static void resume(Client *c, ClientState next)
{
    if (!c->exit_requested)
        enter_state(c, next);
    else if (c->logged_in)
        send_logout(c);
    else
        finish(c, EXIT_SUCCESS);
}

/*
 * Function    : request_exit
 * Description : Stdin ended or a stop signal arrived. A logged-in user is
 *               logged out first; if a reply is still pending, resume()
 *               starts the logout once it has arrived.
 */
static void request_exit(Client *c)
{
    c->exit_requested = 1;
    clock_gettime(CLOCK_MONOTONIC, &c->exit_deadline);
    c->exit_deadline.tv_sec += LOGOUT_TIMEOUT_MS / MS_PER_SECOND;
    c->exit_deadline.tv_nsec += (LOGOUT_TIMEOUT_MS % MS_PER_SECOND) * NS_PER_MS;
    if (c->exit_deadline.tv_nsec >= NS_PER_SECOND)
    {
        c->exit_deadline.tv_sec++;
        c->exit_deadline.tv_nsec -= NS_PER_SECOND;
    }

    if (!c->logged_in && c->state != ST_WAIT_AUTH)
        finish(c, EXIT_SUCCESS);
    else if (state_reads_input(c->state))
        send_logout(c);
}

/*
 * Function    : poll_timeout
 * Description : poll() timeout: forever, or the time left until the exit
 *               deadline once an exit has been requested
 * Return      : Milliseconds, or -1 for no timeout
 */
static int poll_timeout(const Client *c)
{
    struct timespec now;
    long long left;

    if (!c->exit_requested)
        return -1;
    clock_gettime(CLOCK_MONOTONIC, &now);
    left = (long long)(c->exit_deadline.tv_sec - now.tv_sec) * MS_PER_SECOND
           + (c->exit_deadline.tv_nsec - now.tv_nsec) / NS_PER_MS;
    if (left < 0)
        return 0;
    return (left > LOGOUT_TIMEOUT_MS) ? LOGOUT_TIMEOUT_MS : (int)left;
}

/*
 * Function    : trim
 * Description : Removes leading and trailing blanks in place
 * Return      : Pointer to the first non-blank character
 */
static char *trim(char *text)
{
    size_t len;

    while (*text == ' ' || *text == '\t')
        text++;
    len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t'))
        text[--len] = '\0';
    return text;
}

/*
 * Function    : is_back_command
 * Description : True when the line is "/back" (blanks around it allowed)
 */
static int is_back_command(const char *line)
{
    char copy[UI_LINE_BUFFER_SIZE];

    proto_copy_text(copy, sizeof copy, line);
    return strcmp(trim(copy), BACK_COMMAND) == 0;
}

/*
 * Function    : print_reply_block
 * Description : Prints a server reply in the format of the spec screens
 */
static void print_reply_block(const char *text)
{
    fputs("Server reply :\nmessage : ", stdout);
    ui_print_safe(text);
    putchar('\n');
}

/*
 * Function    : print_user_list
 * Description : Prints the list of online users (spec 2.3.3)
 */
static void print_user_list(const UserList *list)
{
    uint32_t i;

    puts("List of Online users");
    if (list->total == 0)
        puts(REPLY_NOBODY_ONLINE);
    for (i = 0; i < list->total; i++)
    {
        fputs("User ", stdout);
        ui_print_safe(list->names[i]);
        fputs(" is ONLINE\n", stdout);
    }
}

/*
 * Function    : async_begin
 * Description : Starts output that was not asked for (an incoming message).
 *               The newline ends a waiting prompt line, or separates the
 *               message from what is above it, as in the spec screens.
 */
static void async_begin(void)
{
    putchar('\n');
}

/*
 * Function    : async_end
 * Description : Shows the waiting prompt again below the incoming message
 */
static void async_end(const Client *c)
{
    const char *prompt = state_prompt(c->state);

    if (prompt != NULL && ui_prompt_visible())
        ui_show_prompt(prompt);
}

/*
 * Function    : echo_input
 * Description : Makes the transcript readable: input from a file or pipe
 *               is not echoed by a terminal, so it is printed here (except
 *               passwords). After a hidden password only a newline is shown.
 */
static void echo_input(const Client *c, const char *line)
{
    if (c->state == ST_PASSWORD)
    {
        putchar('\n');
    }
    else if (!c->stdin_tty)
    {
        ui_print_safe(line);
        putchar('\n');
    }
    ui_prompt_consumed();
}

/*
 * Function    : on_auth_menu
 * Description : Input in the register / login menu
 */
static void on_auth_menu(Client *c, const char *choice)
{
    if (strcmp(choice, CHOICE_REGISTER) == 0 || strcmp(choice, CHOICE_LOGIN) == 0)
    {
        c->auth_option = (strcmp(choice, CHOICE_REGISTER) == 0) ? AUTH_REGISTER : AUTH_LOGIN;
        enter_state(c, ST_USERNAME);
    }
    else if (strcmp(choice, CHOICE_EXIT) == 0)
    {
        finish(c, EXIT_SUCCESS);
    }
    else
    {
        if (choice[0] != '\0')
            puts("Invalid option, please enter 1, 2 or 3");
        enter_state(c, ST_AUTH_MENU);
    }
}

/*
 * Function    : on_username
 * Description : Input of the user name (checked here for a friendly
 *               message; the server checks again)
 */
static void on_username(Client *c, const char *name)
{
    if (!proto_valid_username(name))
    {
        puts(TEXT_BAD_USERNAME);
        enter_state(c, ST_USERNAME);
        return;
    }
    proto_copy_text(c->username, sizeof c->username, name);
    enter_state(c, ST_PASSWORD);
}

/*
 * Function    : on_password
 * Description : Input of the password: sends the request of spec 3.1 and
 *               wipes the password from memory
 */
static void on_password(Client *c, char *password)
{
    int rc;

    if (!proto_valid_password(password))
    {
        puts(TEXT_BAD_PASSWORD);
        enter_state(c, ST_PASSWORD);
        return;
    }
    rc = proto_send_auth(c->sock, c->username, password, c->auth_option);
    memset(password, 0, strlen(password));
    if (rc != PROTO_OK)
    {
        lost_connection(c);
        return;
    }
    enter_state(c, ST_WAIT_AUTH);
}

/*
 * Function    : on_menu
 * Description : Input in the main menu of a logged-in user
 */
static void on_menu(Client *c, const char *choice)
{
    if (strcmp(choice, CHOICE_SINGLE_CHAT) == 0)
    {
        enter_state(c, ST_RECEIVER);
    }
    else if (strcmp(choice, CHOICE_GROUP_CHAT) == 0)
    {
        puts("Group chat: messages go to every online user. Type /back to return to the menu.");
        enter_state(c, ST_GROUP_CHAT);
    }
    else if (strcmp(choice, CHOICE_LOGOUT) == 0)
    {
        send_logout(c);
    }
    else if (strcmp(choice, CHOICE_ONLINE_USERS) == 0)
    {
        if (proto_send_list_request(c->sock) != PROTO_OK)
            lost_connection(c);
        else
            enter_state(c, ST_WAIT_LIST);
    }
    else
    {
        if (choice[0] != '\0')
            puts("Invalid option, please enter 1, 2, 3 or 4");
        enter_state(c, ST_MENU);
    }
}

/*
 * Function    : on_receiver
 * Description : Input of the single chat partner ("/back" or an empty line
 *               returns to the menu)
 */
static void on_receiver(Client *c, const char *name)
{
    if (name[0] == '\0' || strcmp(name, BACK_COMMAND) == 0)
    {
        enter_state(c, ST_MENU);
        return;
    }
    if (!proto_valid_username(name))
    {
        puts(TEXT_BAD_USERNAME);
        enter_state(c, ST_RECEIVER);
        return;
    }
    if (strcmp(name, c->username) == 0)
    {
        puts(REPLY_SELF_MESSAGE);
        enter_state(c, ST_RECEIVER);
        return;
    }
    proto_copy_text(c->receiver, sizeof c->receiver, name);
    printf("Single chat with %s. Type /back to return to the menu.\n", c->receiver);
    enter_state(c, ST_SINGLE_CHAT);
}

/*
 * Function    : on_message
 * Description : A line typed in single or group chat. Control characters
 *               (such as tabs) are sent as spaces.
 */
static void on_message(Client *c, char *text)
{
    uint32_t option = (c->state == ST_SINGLE_CHAT) ? CHAT_SINGLE : CHAT_GROUP;
    char *p;

    if (is_back_command(text))
    {
        enter_state(c, ST_MENU);
        return;
    }
    if (proto_text_is_blank(text))
    {
        enter_state(c, c->state);
        return;
    }
    if (strlen(text) >= PROTO_TEXT_SIZE)
    {
        printf("Message too long (max %d characters)\n", PROTO_TEXT_SIZE - 1);
        enter_state(c, c->state);
        return;
    }
    for (p = text; *p != '\0'; p++)
    {
        if ((unsigned char)*p < ' ' || *p == ASCII_DEL)
            *p = ' ';
    }
    if (proto_send_chat(c->sock, c->username, option == CHAT_SINGLE ? c->receiver : "",
                        text, option) != PROTO_OK)
    {
        lost_connection(c);
        return;
    }
    c->chat_state = c->state;
    enter_state(c, ST_WAIT_SEND);
}

/*
 * Function    : handle_line
 * Description : Dispatches one input line to the handler of the state
 */
static void handle_line(Client *c, char *line)
{
    echo_input(c, line);
    switch (c->state)
    {
    case ST_AUTH_MENU:
        on_auth_menu(c, trim(line));
        break;
    case ST_USERNAME:
        on_username(c, trim(line));
        break;
    case ST_PASSWORD:
        on_password(c, line);
        break;
    case ST_MENU:
        on_menu(c, trim(line));
        break;
    case ST_RECEIVER:
        on_receiver(c, trim(line));
        break;
    case ST_SINGLE_CHAT:
    case ST_GROUP_CHAT:
        on_message(c, line);
        break;
    default:
        break;
    }
}

/*
 * Function    : on_reply
 * Description : A server reply (spec 3.2 / 3.6). Its meaning depends on
 *               the request the client is waiting for.
 */
static void on_reply(Client *c, const ServerReply *reply)
{
    int ok = (reply->code == REPLY_OK);

    if (reply->code == REPLY_FATAL)
    {
        if (ui_prompt_visible())
            putchar('\n');
        print_reply_block(reply->message);
        finish(c, EXIT_FAILURE);
        return;
    }

    switch (c->state)
    {
    case ST_WAIT_WELCOME:
        if (!ok)
        {
            print_reply_block(reply->message);
            finish(c, EXIT_FAILURE);
            return;
        }
        putchar('\n');
        resume(c, ST_AUTH_MENU);
        break;

    case ST_WAIT_AUTH:
        print_reply_block(reply->message);
        if (ok)
        {
            c->logged_in = 1;
            enter_state(c, ST_WAIT_LIST);
        }
        else
        {
            resume(c, ST_AUTH_MENU);
        }
        break;

    case ST_WAIT_SEND:
        ui_print_safe(reply->message);
        putchar('\n');
        resume(c, c->chat_state);
        break;

    case ST_WAIT_LOGOUT:
        print_reply_block(reply->message);
        if (ok)
            c->logged_in = 0;
        if (c->exit_requested)
            finish(c, EXIT_SUCCESS);
        else
            enter_state(c, ok ? ST_AUTH_MENU : ST_MENU);
        break;

    default:
        async_begin();
        print_reply_block(reply->message);
        async_end(c);
        break;
    }
}

/*
 * Function    : on_user_list
 * Description : The list of online users, after login or option 4
 */
static void on_user_list(Client *c, const UserList *list)
{
    if (c->state == ST_WAIT_LIST)
    {
        print_user_list(list);
        resume(c, ST_MENU);
        return;
    }
    async_begin();
    print_user_list(list);
    async_end(c);
}

/*
 * Function    : on_user_status
 * Description : Another user came online or went offline (spec 2.3.4, 2.3.6)
 */
static void on_user_status(const Client *c, const StatusNotice *notice)
{
    async_begin();
    fputs("Incoming Message from server :\nUser ", stdout);
    ui_print_safe(notice->username);
    fputs(notice->status == STATUS_ONLINE ? " is ONLINE\n" : " is OFFLINE\n", stdout);
    async_end(c);
}

/*
 * Function    : on_chat
 * Description : A message from another user; private messages are marked
 */
static void on_chat(const Client *c, const ChatMessage *msg)
{
    async_begin();
    fputs("Incoming Message from ", stdout);
    ui_print_safe(msg->sender);
    fputs(msg->chat_option == CHAT_SINGLE ? " (private) :\n" : " :\n", stdout);
    ui_print_safe(msg->message);
    putchar('\n');
    async_end(c);
}

/*
 * Function    : on_socket
 * Description : Receives and handles one packet from the server
 */
static void on_socket(Client *c)
{
    Packet pkt;
    int rc = proto_recv_packet(c->sock, &pkt);

    if (rc == PROTO_OK)
    {
        switch (pkt.type)
        {
        case PKT_SERVER_REPLY:
            on_reply(c, &pkt.body.reply);
            return;
        case PKT_USER_LIST:
            on_user_list(c, &pkt.body.list);
            return;
        case PKT_USER_STATUS:
            on_user_status(c, &pkt.body.status);
            return;
        case PKT_CHAT:
            on_chat(c, &pkt.body.chat);
            return;
        default:
            rc = PROTO_ERR_BAD;
            break;
        }
    }

    if (c->exit_requested && c->state == ST_WAIT_LOGOUT)
    {
        finish(c, EXIT_SUCCESS);
        return;
    }
    if (ui_prompt_visible())
        putchar('\n');
    puts(rc == PROTO_ERR_BAD ? "Protocol error: invalid data from the server"
                             : "Connection closed by the server");
    finish(c, EXIT_FAILURE);
}

/*
 * Function    : on_signal
 * Description : Ctrl+C, SIGTERM or SIGHUP: log out and exit; a second
 *               signal exits at once
 */
static void on_signal(Client *c)
{
    signal_pipe_drain(c->signal_fd);
    if (c->exit_requested)
    {
        finish(c, EXIT_SUCCESS);
        return;
    }
    if (ui_prompt_visible())
    {
        putchar('\n');
        ui_prompt_consumed();
    }
    request_exit(c);
}

/*
 * Function    : process_buffered_input
 * Description : Handles the input lines already buffered, as long as the
 *               state reads input. Piped input with many lines is thus
 *               processed strictly in order, one state at a time.
 */
static void process_buffered_input(Client *c)
{
    char line[UI_LINE_BUFFER_SIZE];

    while (state_reads_input(c->state))
    {
        LineStatus status = line_reader_next(&c->input, line, sizeof line);

        if (status == LINE_READY)
        {
            handle_line(c, line);
        }
        else if (status == LINE_TOO_LONG)
        {
            if (c->state == ST_PASSWORD || ui_prompt_visible())
                putchar('\n');
            ui_prompt_consumed();
            printf("Input line too long (max %d characters)\n", UI_LINE_MAX);
            enter_state(c, c->state);
        }
        else if (status == LINE_EOF)
        {
            request_exit(c);
        }
        else
        {
            break;
        }
    }
    memset(line, 0, sizeof line);       /* It may have held a password */
}

/*
 * Function    : run
 * Description : Main loop: handle buffered input, then wait with poll()
 *               for the socket, a signal or (in input states) stdin
 */
static void run(Client *c)
{
    struct pollfd fds[POLL_COUNT];
    nfds_t count;
    int rc;

    while (c->state != ST_DONE)
    {
        process_buffered_input(c);
        if (c->state == ST_DONE)
            break;

        fds[POLL_SOCKET].fd = c->sock;
        fds[POLL_SOCKET].events = POLLIN;
        fds[POLL_SIGNAL].fd = c->signal_fd;
        fds[POLL_SIGNAL].events = POLLIN;
        fds[POLL_STDIN].fd = STDIN_FILENO;
        fds[POLL_STDIN].events = POLLIN;
        fds[POLL_STDIN].revents = 0;
        count = (state_reads_input(c->state) && !c->input.eof) ? POLL_COUNT : POLL_STDIN;

        fflush(stdout);
        rc = poll(fds, count, poll_timeout(c));
        if (rc < 0)
        {
            if (errno == EINTR)
                continue;
            perror("poll");
            finish(c, EXIT_FAILURE);
            break;
        }
        if (rc == 0)
        {
            puts("No reply from the server, exiting");
            finish(c, EXIT_FAILURE);
            break;
        }
        if (fds[POLL_SIGNAL].revents != 0)
            on_signal(c);
        if (c->state != ST_DONE && fds[POLL_SOCKET].revents != 0)
            on_socket(c);
        if (c->state != ST_DONE && fds[POLL_STDIN].revents != 0)
            line_reader_fill(&c->input, STDIN_FILENO);
    }
}

/*
 * Function    : main
 * Description : Connects to the server and runs the client
 */
int main(int argc, char *argv[])
{
    static const int stop_signals[] = { SIGINT, SIGTERM, SIGHUP };
    const char *prog = (argc > 0 && argv[0] != NULL) ? argv[0] : "client";
    const char *host = CHAT_DEFAULT_SERVER_IP;
    const char *port = CHAT_DEFAULT_PORT_TEXT;
    char error[TCP_ERROR_TEXT_SIZE];
    uint16_t port_number;
    Client c;

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        print_usage(stdout, prog);
        return EXIT_SUCCESS;
    }
    if (argc > 3)
    {
        print_usage(stderr, prog);
        return EXIT_FAILURE;
    }
    if (argc > 1)
        host = argv[1];
    if (argc > 2)
        port = argv[2];
    if (tcp_parse_port(port, &port_number) != 0)
    {
        fprintf(stderr, "Error: Invalid port '%s' (use 1-65535).\n", port);
        return EXIT_FAILURE;
    }

    memset(&c, 0, sizeof c);
    c.sock = tcp_connect(host, port, error, sizeof error);
    if (c.sock < 0)
    {
        fprintf(stderr, "Error: Could not connect to %s port %s: %s\n", host, port, error);
        return EXIT_FAILURE;
    }
    puts("Socket created");
    puts("Connected");

    c.signal_fd = signal_pipe_init(stop_signals, sizeof stop_signals / sizeof stop_signals[0]);
    if (c.signal_fd < 0 || atexit(ui_echo_restore) != 0
        || tcp_set_recv_timeout(c.sock, RECV_TIMEOUT_SEC) != 0)
    {
        perror("Error: Could not set up signal handling or the socket timeout");
        close(c.sock);
        return EXIT_FAILURE;
    }
    c.stdin_tty = isatty(STDIN_FILENO);
    c.state = ST_WAIT_WELCOME;
    c.exit_status = EXIT_SUCCESS;
    line_reader_init(&c.input);

    run(&c);

    ui_echo_restore();
    fflush(stdout);
    close(c.sock);
    close(c.signal_fd);
    return c.exit_status;
}
