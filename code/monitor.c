/*_______________________________INCLUDES_____________________________________*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>

/*_______________________________DEFINES_____________________________________*/

#define MAX_DAEMON 100
#define MAX_WORDS 100
#define MAX_LINHA 512
#define MAX_COMANDO 200
#define MAX_ARGS 200
#define MAX_NAME 100

/*_____________________________ESTRUTURAS___________________________________*/

/* ESTRUTURA PARA GUARDAR STRINGS A MONITORIZAR */
typedef struct sWords{
	char word[MAX_ARGS];
}Words;


static char *promptTAG = "ECHELINHO> ";
static char *line_read = (char *)NULL;


/* ESTRUTURA PARA GUARDAR ESTADO DO SISTEMA */
typedef struct sDaemon{
	int pid;
	int nrwords;
	Words words[MAX_WORDS];
	char file[MAX_NAME];
	char filewords[MAX_NAME];
}Daemon;

/*__________________________VARIAVEIS GLOBAIS_______________________________*/

/* VARIAVEIS GLOBAIS */
Daemon daemons[MAX_DAEMON];
int nrdaemon;
int fifofd;
char* fifoname = "fifo";
char* fmonitorname = "pidmonitor";
char* save = "savepids";
char* nmname = "notnotified";

/*__________________________FUNCOES AUXILIARES_______________________________*/


/* LEITOR DE COMANDOS*/

char *rl_gets ()
{
  /* If the buffer has already been allocated,
     return the memory to the free pool. */
  if (line_read)
    {
      free (line_read);
      line_read = (char *)NULL;
    }

  /* Get a line from the user. */
  line_read = readline (promptTAG);

  /* If the line has any text in it,
     save it on the history. */
  if (line_read && *line_read)
    add_history (line_read);

  return (line_read);
}



/* CRIA FIFO */
void createfifo(){
	fifofd = open(fifoname,O_NONBLOCK | O_RDONLY);

	if(fifofd<0){
		int fich = mkfifo(fifoname,0666);
		fifofd = open(fifoname,O_NONBLOCK | O_RDONLY);
	}
}

/* EVENTO FICHEIRO LOG MODIFICADO */
void logmodified(){
	char s[MAX_LINHA];
	read(fifofd,s,sizeof(s));
	printf("%s",s);
	printf("%s",promptTAG);
}

/* CARREGAR ESTADO DO SISTEMA */
void loadpids(char* f){
	
	int file = open(f,O_RDONLY);
	int i,j;

	if(file>0){ 
		read(file,&nrdaemon,sizeof(int));
		read(file,&daemons,sizeof(daemons));

		close(file);
	}
}

/* GRAVAR ESTADO DO SISTEMA */
void savepids(char* f){
	int file = open(f, O_WRONLY);
	int i,j;

	if(file<0){
		creat(f,0777);
		file = open(f, O_WRONLY);
	}

	write(file,&nrdaemon,sizeof(int));
	write(file,&daemons,sizeof(daemons));

	close(file);
}

/* GRAVA PID DO MONITOR */
void gravamonitor(){
	int pidmonitor = getpid();
	int fdmonitor = open(fmonitorname, O_WRONLY);

	if(fdmonitor<0){
		creat(fmonitorname,0666);
		fdmonitor = open(fmonitorname, O_WRONLY);
	}

	write(fdmonitor,&pidmonitor,sizeof(int));
	close(fdmonitor);
}

/* VERIFICA A EXISTENCIA DE EVENTOS ENQUANTO MONITOR ESTAVA FECHADO */
void readbackup(char* backupname){
	char linhabackup[MAX_LINHA];
	while(read(fifofd,linhabackup,sizeof(linhabackup))){
		printf("%s",linhabackup);
		memset(linhabackup,'\0',sizeof(linhabackup));
	}
}

/* PRINT DE ERROS */
void printerror(char* error){
   fprintf (stderr,"ERRO: %s.\n",error);
}

/* INICIA MONITORIZACAO DE UM FICHEIRO */
void newdaemon(char* args){
	char file[200];
	char words[200];

	/* VERIFICA ARGUMENTOS */
	int nrargs = sscanf(args,"%s %[^\n]",file,words);
	if (nrargs!=2){
		printerror("Introduza o nome do ficheiro e respectiva string a monitorizar");
		return;
	}

	/* CRIA NOME FICHEIRO PARA GUARDAR STRINGS */
	char filewordsname[strlen(file)+6];
	strcpy(filewordsname,file);
	strcat(filewordsname,"words");

	/* PREENCHE ESTRUTURA DE ESTADO */
	strcpy(daemons[nrdaemon].words[0].word,words);
	daemons[nrdaemon].nrwords = 1;
	strcpy(daemons[nrdaemon].file,file);
	strcpy(daemons[nrdaemon].filewords,filewordsname);

	/* CRIA E GRAVA STRING */
	creat(filewordsname,0666);
	int filewords = open(filewordsname,O_WRONLY);
	write(filewords,&daemons[nrdaemon].nrwords,sizeof(int));
	write(filewords,daemons[nrdaemon].words[0].word,sizeof(char)*MAX_ARGS);
	close(filewords);

	/* INICIA PROCESSO DAEMON */
	daemons[nrdaemon].pid = fork();
	if (!daemons[nrdaemon].pid){
		execlp("./daemon","./daemon",file,fmonitorname,fifoname,nmname,NULL);
		printerror("ERRO: Impossivel de monitorizar\n");
		exit (0);
	}
	
	nrdaemon++;
}

/* FECHA MONITOR*/
void closemonitor(){
	int i;

	/* AVISA DAEMONS QUE VAI FECHAR */
	for(i=0;i<nrdaemon;i++){
		kill(daemons[i].pid,SIGUSR1);
	}

	savepids(save);
	close(fifofd);
	exit(0);
}

/* ABRE MONITOR */
void monitoropened(){
	loadpids(save);

	/* AVISA DAEMONS QUE MONITOR FOI ABERTO */
	int i;
	for(i=0;i<nrdaemon;i++){
		kill(daemons[i].pid,SIGUSR1);
	}
}


/* TEMINAR PROGRAMA */
void secureexit(){
	int i;
	/* MATA DAEMONS */
	for(i=0;i<nrdaemon;i++){
		kill(daemons[i].pid,SIGKILL);
		remove(daemons[i].filewords);
	}

	/* APAGA FICHEIROS */
	close(fifofd);
	remove(fifoname);
	remove(fmonitorname);
	remove(save);
	remove(nmname);
	exit(0);
}

/* ADICIONA STRING A MONITORIZAR*/
void addstring(char* args){
	char file[200];
	char words[200];

	/* VERIFICA ARGUMENTOS */
	int nrargs =sscanf(args,"%s %[^\n]",file,words);
	if (nrargs!=2){
		printerror("Introduza o nome do ficheiro e respectiva nova string a monitorizar");
		return;
	}

	/* ACTUALIZA FICHEIRO DE STRINGS */
	int i;
	for(i=0;i<nrdaemon;i++){
		if(strcmp(file,daemons[i].file)==0){
			strcpy(daemons[i].words[daemons[i].nrwords].word,words);
			daemons[i].nrwords++;
			int filewords = open(daemons[i].filewords,O_WRONLY);

			write(filewords,&daemons[i].nrwords,sizeof(int));

			int nr = daemons[i].nrwords-1;
   			while(nr>=0){
   				write(filewords,daemons[i].words[nr].word,sizeof(char)*MAX_ARGS);
				nr--;
   			}
   			close(filewords);

   			/* AVISA DAEMONS QUE FOI ADICIONADA STRING */
			kill(daemons[i].pid,SIGUSR2);
			return;
		}
	}
	printerror("Ficheiro não está a ser monitorizado");
}

/* CLOSE DAEMON*/
void closedaemon(char* args){
	/* ACTUALIZA FICHEIRO DE STRINGS */
	int i;
	for(i=0;i<nrdaemon;i++){
		if(strcmp(daemons[i].file,args)==0){
   			/* MATA DAEMON */
			kill(daemons[i].pid,SIGKILL);
			remove(daemons[i].filewords);
			i++;
			for(;i<nrdaemon;i++){
				daemons[i-1] = daemons[i];
			}

			nrdaemon--;
			return;
		}
	}
	printerror("Ficheiro não está a ser monitorizado");
}

/* REMOVE STRING A MONITORIZAR */
void removestring(char* args){
	char file[200];
	char words[200];

	/* VERIFICA ARGUMENTOS */
	int nrargs = sscanf(args,"%s %[^\n]",file,words);
	if (nrargs!=2){
		printerror("Introduza o nome do ficheiro e respectiva string a remover");
		return;
	}

	/* PROCURA FICHEIRO E STRING */
	int i,j;
	for(i=0;i<nrdaemon;i++){
		if(strcmp(file,daemons[i].file)==0){
			for(j=0;j<daemons[i].nrwords;j++){
				if(strcmp(daemons[i].words[j].word,words)==0){
					j++;
					for(;j<daemons[i].nrwords;j++){
						strcpy(daemons[i].words[j-1].word,daemons[i].words[j].word);
						daemons[i].nrwords--;

						/* ACTUALIZA FICHEIRO DE STRINGS */
					    int filewords = open(daemons[i].filewords,O_WRONLY);
			   			write(filewords,&daemons[i].nrwords,sizeof(int));
			    		int nr = daemons[i].nrwords-1;
   						while(nr>=0){
   							write(filewords,daemons[i].words[nr].word,sizeof(char)*MAX_ARGS);
							nr--;
   						}
   						close(filewords);

   						/* AVISA RESPECTIVO DAEMON DA MUDANÇA */
						kill(daemons[i].pid,SIGUSR2);
						return;
					}
				}
			}
		}
	}
	printerror("Ficheiro ou string não está a ser monitorizado(a)");
}

/* IMPRIME O ESTADO ACTUAL DO SISTEMA */
void estado(){
	clean(50);
	printf(">      ESTADO ACTUAL:\n\n");
	printf("Total de monitorizações: %d\n\n",nrdaemon);

	int i,j;
	for(i=0;i<nrdaemon;i++){
		printf("Monitorização %d\nFicheiro Monitorizado: %s\nStrings monitorizadas: ",i+1,daemons[i].file);
		for(j=0;j<daemons[i].nrwords-1;j++){
			printf("%s, ",daemons[i].words[j].word);
		}
		printf("%s\n\n",daemons[i].words[j].word);
	}
}

/* LIMPA ECRA */
int clean (int tamanho){
	int i=0;
	
	while (i<tamanho)
	{
		fputs("\033[A\033[2K\033[A\033[2K",stdout);
		i++;
	}
	return 0;
}

/*__________________________INTERPRETADOR E MAIN_______________________________*/


/* INTERPRETADOR DE COMANDOS */
void prompt(){
	char *linhacomando;
	char comando[MAX_COMANDO];
	char args[MAX_ARGS];

	int nrargs;

	while(linhacomando = rl_gets()){
		

		nrargs = sscanf(linhacomando,"%s %[^\n]",comando,args);

		if(strcmp(comando,"NEWDAEMON")==0){
			newdaemon(args);
		} else if(strcmp(comando,"CLOSEMONITOR")==0){
			closemonitor();
		} else if(strcmp(comando,"ADDSTRING")==0){
			addstring(args);
		} else if(strcmp(comando,"REMOVESTRING")==0){
			removestring(args);
		} else if(strcmp(comando,"CLOSEDAEMON")==0){
			closedaemon(args);
		} else if(strcmp(comando,"ESTADO")==0){
			estado();
		} else if(strcmp(comando,"EXIT")==0){
			secureexit();
		} else{
			printerror("Comando inválido");
		}
		strcpy(linhacomando,"");
		strcpy(comando,"");
		strcpy(args,"");
	}

}

/* MAIN */
int main(int argc, char *argv[]){
	/* LIMPA ECRA */
	clean(50);

	/* CRIAR E ABRIR FIFO */
	createfifo();

	/* GRAVAR PID MONITOR */
	gravamonitor();

	/* PRONTO A RECEBER SINAL */
	signal(SIGUSR1, logmodified);
	signal(SIGINT, SIG_IGN);

	/* AVISA OS DAEMON QUE MONITOR FOI ABERTO */
	monitoropened();

	/* VE SE HÁ BACKUP, E IMPRIME */
	readbackup(fifoname);

	/* LE COMANDOS */
	prompt();
}