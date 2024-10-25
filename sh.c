#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXARGS 10
#define EXEC 1
#define REDIR 2
#define PIPE 3
#define LIST 4
#define BACK 5

char tokentype;
char *tokenstart, *tokenend;
char *es;

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

void gettoken() {
  char *s = tokenend;

  while (s < es && strchr(whitespace, *s)) {
    s++;
  }

  tokentype = *s;
  tokenstart = s;

  // since >> is a token
  if (*s == '>') {
    s++;
    if (*s == '>') {
      tokentype = '+';
      s++;
    }
  } else if (s < es && strchr(symbols, *s)) {
    s++;
  } else if (s < es) {
    tokentype = 'a';
    while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s)) {
      s++;
    }
  }
  tokenend = s;
}

void panic(char *s) {
  fprintf(stderr, "%s\n", s);
  exit(1);
}

int fork1(void) {
  int pid;

  pid = fork();
  if (pid == -1)
    panic("fork");
  return pid;
}

struct cmd {
  int type;
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  char mode;
  FILE *fp;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

struct cmd *redircmd(struct cmd *subcmd, char *file, char *efile, char mode,
                     FILE *fp) {
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fp = fp;
  return (struct cmd *)cmd;
}

struct cmd *execcmd(void) {
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd *)cmd;
}

struct cmd *pipecmd(struct cmd *left, struct cmd *right) {
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *listcmd(struct cmd *left, struct cmd *right) {
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *backcmd(struct cmd *subcmd) {
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd *)cmd;
}

struct cmd *nulterminate(struct cmd *cmd);

struct cmd *parseredirs(struct cmd *cmd) {
  char prevtokentype;

  while (tokenstart < es && strchr("<>", *tokenstart)) {
    prevtokentype = tokentype;
    gettoken();
    if (tokentype != 'a')
      panic("missing file for redirection");
    switch (prevtokentype) {
    case '<':
      cmd = redircmd(cmd, tokenstart, tokenend, 'r', stdin);
      break;
    case '>':
      cmd = redircmd(cmd, tokenstart, tokenend, 'w', stdout);
      break;
    case '+':
      cmd = redircmd(cmd, tokenstart, tokenend, 'a', stdout);
      break;
    }
    gettoken();
  }
  return cmd;
}

struct cmd *parseblock();

struct cmd *parseexec() {
  struct execcmd *cmd;
  struct cmd *ret;
  int argc;

  argc = 0;
  ret = execcmd();
  cmd = (struct execcmd *)ret;

  gettoken();
  if (*tokenstart == '(') {
    return parseblock();
  }

  ret = parseredirs(ret);

  while (tokenstart < es && !strchr("|)&;", *tokenstart)) {
    if (tokentype != 'a')
      panic("syntax");
    cmd->argv[argc] = tokenstart;
    cmd->eargv[argc] = tokenend;
    argc++;
    if (argc >= MAXARGS)
      panic("too many args");
    gettoken();
    ret = parseredirs(ret);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

struct cmd *parsepipe() {
  struct cmd *cmd;

  cmd = parseexec();
  if (*tokenstart == '|')
    cmd = pipecmd(cmd, parsepipe());
  return cmd;
}

struct cmd *parseline() {
  struct cmd *cmd;

  cmd = parsepipe();
  while (*tokenstart == '&') {
    cmd = backcmd(cmd);
    gettoken();
  }
  if (*tokenstart == ';')
    cmd = listcmd(cmd, parseline());
  return cmd;
}

struct cmd *parseblock() {
  struct cmd *cmd;

  if (*tokenstart != '(')
    panic("parseblock");
  cmd = parseline();
  if (*tokenstart != ')')
    panic("syntax - missing )");
  gettoken();
  cmd = parseredirs(cmd);
  return cmd;
}

void runcmd(struct cmd *cmd) {
  char path[] = "/bin/";
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    exit(1);

  switch (cmd->type) {
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd *)cmd;
    if (ecmd->argv[0] == 0)
      exit(1);
    char command[sizeof(path) + sizeof(ecmd->argv[0]) + 1];
    strcpy(command, path);
    strcat(command, ecmd->argv[0]);
    execv(command, ecmd->argv);
    fprintf(stderr, "exec %s failed\n", ecmd->argv[0]);
    break;
  case REDIR: {
    rcmd = (struct redircmd *)cmd;
    // stdin or stdout now points to file
    if (freopen(rcmd->file, &rcmd->mode, rcmd->fp) < 0) {
      fprintf(stderr, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;
  }
  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    if (pipe(p) < 0)
      panic("pipe");
    if (fork1() == 0) {
      dup2(p[1], STDOUT_FILENO);
      close(p[0]);
      close(p[1]);
      // runcmd will exit so the code below wont run for this process
      runcmd(pcmd->left);
    }
    if (fork1() == 0) {
      dup2(p[0], STDIN_FILENO);
      close(p[0]);
      close(p[1]);
      // runcmd will exit so the code below wont run for this process
      runcmd(pcmd->right);
    }

    close(p[0]);
    close(p[1]);
    wait(NULL);
    wait(NULL);
    break;
  case LIST:
    lcmd = (struct listcmd *)cmd;
    if (fork1() == 0)
      // runcmd will exit so the code below wont run for this process
      runcmd(lcmd->left);
    wait(NULL);
    runcmd(lcmd->right);
    break;
  case BACK:
    bcmd = (struct backcmd *)cmd;
    if (fork1() == 0)
      // runcmd will exit so the code below wont run for this process
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

struct cmd *parsecmd(char *s) {
  struct cmd *cmd;

  tokenend = s;
  es = s + strlen(s);
  cmd = parseline();

  if (tokenend != es) {
    fprintf(stderr, "leftovers: %s\n", s);
    panic("syntax");
  }

  nulterminate(cmd);
  return cmd;
}

int main() {
  char *input;
  size_t n;

  while (1) {
    printf("prompt> ");
    if (getline(&input, &n, stdin) == -1)
      return 1;

    if (strncmp(input, "exit", 4) == 0) {
      exit(0);
    } else if (strncmp(input, "cd ", 3) == 0) {
      input[strlen(input) - 1] = 0; // remove /n
      if (chdir(input + 3) < 0)
        fprintf(stderr, "cannot cd %s\n", input + 3);
      continue;
    }

    if (fork1() == 0) {
      runcmd(parsecmd(input));
    } else {
      wait(NULL);
    }
  }

  free(input);
  return 0;
}

struct cmd *nulterminate(struct cmd *cmd) {
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    return 0;

  switch (cmd->type) {
  case EXEC:
    ecmd = (struct execcmd *)cmd;
    for (i = 0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;
  case REDIR:
    rcmd = (struct redircmd *)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;
  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;
  case LIST:
    lcmd = (struct listcmd *)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;
  case BACK:
    bcmd = (struct backcmd *)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
