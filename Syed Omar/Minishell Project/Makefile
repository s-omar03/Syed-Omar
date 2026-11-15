CC     = gcc
CFLAGS = -Wall -Wextra -g
OBJS   = minishell.o job_control.o

msh: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c minishell.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f msh $(OBJS)
