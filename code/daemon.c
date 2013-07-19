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
#include <sys/time.h>

/*_______________________________DEFINES_____________________________________*/

#define _GNU_SOURCE
#define LINHA_SIZE 512
#define MAX_ARGS 200
#define MAX_COMANDO 200
#define MAX_WORDS 100

/*_____________________________ESTRUTURAS___________________________________*/

/* ESTRUTURA PARA GUARDAR STRINGS A MONITORIZAR */
typedef struct sWords{
   char word[MAX_ARGS];
}Words;

/* ESTRUTURA PARA GUARDAR AS MUDANÇAS NAO MONITORIZADAS NO ULTIMO MINUTO */
typedef struct sNotMonitorized{
   char line[LINHA_SIZE];
   struct timeval time;
   int linha;
   struct sNotMonitorized *next;
}*NotMonitorized;

/*__________________________VARIAVEIS GLOBAIS_______________________________*/

NotMonitorized nmlist;
int pidmonitor=0;
int filetowrite;
char filename[FILENAME_MAX];
char fifoname[FILENAME_MAX];
char nmname[FILENAME_MAX];
char fmonitorname[FILENAME_MAX];
char towrite[LINHA_SIZE];
char filewordsname[MAX_COMANDO];
Words words[MAX_WORDS];
int nrwords=0;
int nrlinha=0;
char buffline[LINHA_SIZE];

void get_event (int file, int fd, const char * target);
void handle_error (int error);

/*__________________________FUNCOES AUXILIARES_______________________________*/

/* INICIA DAEMON */
void load(char* namemonitor, char* fifo, char* file, char* nm){
   /* GRAVA NOMES DE FICHEIROS RELACIONADOS */
   strcpy(filename,file);
   strcpy(fmonitorname,namemonitor);
   strcpy(fifoname,fifo);
   strcpy(nmname,nm);

   /* CRIA NOME DE FICHEIRO PARA LER STRINGS A MONITORIZAR */
   strcat(filewordsname,file);
   strcat(filewordsname,"words");

   /* LE STRINGS A MONITORIZAR */
   int filewords = open(filewordsname,O_RDONLY);
   read(filewords,&nrwords,sizeof(int));
   read(filewords,words[nrwords-1].word,sizeof(char)*MAX_ARGS);
   close(filewords);

   /* LE PID DO MONITOR */
   int fdmonitor = open(fmonitorname,O_RDONLY);
   read(fdmonitor,&pidmonitor,sizeof(int));
   close(fdmonitor);

   /* ABRE FIFO */
   filetowrite = open(fifoname,O_WRONLY);
}

/* MONITOR FOI FECHADO */
void monitorclosed(){
   //fprintf(stderr,"CLOSED\n");
   pidmonitor=0;
}

/* MONITOR FOI ABERTO */
void monitoropened(){
   /* FECHA FIFO PARA READ DO MONITOR ENCONTRAR EOF */
   close(filetowrite);

   //fprintf(stderr,"OPENDED\n");

   /* LE NOVO PID DO MONITOR */
   int fdmonitor = open(fmonitorname,O_RDONLY);
   read(fdmonitor,&pidmonitor,sizeof(int));
   close(fdmonitor);

   /* ESPERA QUE MONITOR ACABE DE LER */
   sleep(1);

   /* VOLTA A ABRIR FIFO */
   filetowrite = open(fifoname,O_WRONLY);
}

/* IGNORA E CONTA LINHAS ANTES DA MONITORIZAÇAO */
void ignorelines(int fd){
   dup2(fd,0);
   while (fgets(buffline, sizeof(buffline), stdin)>0){
      nrlinha++;
   }
   nrlinha++;
   dup2(0,fd);
}

/* VERIFICA OCORRENCIAS DA NOVA STRING NO ULTIMO MINUTO*/
void notificanm(){
   NotMonitorized aux = nmlist;

   while(aux){
      int i=0;
      int j=nrwords-1;

      /* VERIFICA SE CONTEM STRING */
      while (j>=0 && i==0){
         if(strstr(aux->line,words[j].word)!=NULL)
            i++;
         j--;
      }    

      /* SE CONTEM, ESCREVE NO PIPE E AVISA MONITOR PARA IMPRIMIR*/
      if (i>0){
         strcat(towrite,"OLDINFO: ");
         strcat(towrite,"O ficheiro monitorizado (");
         strcat(towrite,filename);
         strcat(towrite,") foi alterado, adicionada na linha ");
               
         char buf[5];
         sprintf(buf,"%d",aux->linha);
               
         strcat(towrite,buf);
         strcat(towrite," a seguinte string :\n");
         strcat(towrite,aux->line);

         write(filetowrite,towrite,sizeof(towrite));
         memset(towrite,'\0',LINHA_SIZE);

         if(pidmonitor!=0){
            kill(pidmonitor, SIGUSR1);
            }
         }

      aux = aux->next;
   }
}

/* ACTUALIZA LISTA DE STRINGS MONITORIZADAS*/
void actwords(){
   int filewords = open(filewordsname,O_RDONLY);
   int i;


   nrwords =0;

   /* LE NOVA LISTA DE STRINGS*/
   read(filewords,&i,sizeof(int));
   while(nrwords<i){
      read(filewords,words[nrwords].word,sizeof(char)*MAX_ARGS);
      nrwords++;
   }
   close(filewords);

   /* VERIFICA OCORRENCIAS DA NOVA STRING NO ULTIMO MINUTO*/
   notificanm();
}

/* VERIFICA ACTUALIZACAO NO MONITOR*/
void actmonitor(){
   if(pidmonitor==0)
      monitoropened();
   else
      monitorclosed();
}

/* LIMPA LISTA DE EVENTOS NAO MONITORIZADOS COM MAIS DE UM MINUTO*/
void limpaNM(){
   struct timeval timev;
   gettimeofday(&timev, NULL);
   
   NotMonitorized aux = nmlist;

   /* QUANDO ENCONTRAR UM QUE FOI Á MAIS DE UM MINUTO APAGA OS SEGUINTES */
   while(aux){
      if(difftime(timev.tv_sec,aux->time.tv_sec)>60){
         free(aux);
      }
      else
         aux = aux->next;
   }
   alarm(60);
}

/*__________________________NOTIFICADOR E MAIN_______________________________*/

int main (int argc, char *argv[])
{
   /* VERIFICA ARGUMENTOS */
   if(argc<4) return 0;
  
   /* VARIAVEIS AUXILIARES */
   char target[FILENAME_MAX];
   int fd,wd;  

   /* TENTA ABRIR O FICHEIRO E APONTA PARA O FINAL DELE */
   int file = open(argv[1],O_RDONLY);
   if(file==-1){
      creat(argv[1],0666);
      file = open(argv[1],O_RDONLY);
   }  

   /* IGNORA LINHAS NAO MONITORIZADAS E APONTA PARA FINAL DE FICHEIRO*/
   ignorelines(file);

   /* OUTRAS INICIAÇOES */
   load(argv[2],argv[3],argv[1],argv[4]);
   strcpy (target, argv[1]);

   /* PRONTO A RECEBER SINAIS DO MONITOR */
   signal(SIGUSR1, actmonitor);
   signal(SIGUSR2, actwords);
   signal(SIGALRM, limpaNM);

   /* ENVIA SINAL DEPOIS DE 1 MINUTO*/
   alarm(60);

   /* ESCREVE INFORMAÇAO QUE COMEÇOU A MONITORIZAR */
   strcat(towrite,"INFO: A monitorizar o ficheiro ");
   strcat(towrite,target);
   strcat(towrite,"...\n");
   write(filetowrite,towrite,sizeof(towrite));
   memset(towrite,'\0',LINHA_SIZE);
   if(pidmonitor!=0) 
      kill(pidmonitor, SIGUSR1);

   /* INOTIFY INIT AND WATCH */
   fd = inotify_init();
   if (fd < 0) {
      handle_error (errno);
      return 1;
   }
   wd = inotify_add_watch (fd, target, IN_ALL_EVENTS);
   if (wd < 0) {
      handle_error (errno);
      return 1;
   }

   /* TENTA LER NOVAS MUDANÇAS NO FICHEIRO */
   while (1) {
      get_event(file, fd, target);
   }

   return 0;
}

/* PERMITE LEITURA DE 1000 EVENTOS */
#define BUFF_SIZE ((sizeof(struct inotify_event)+FILENAME_MAX)*1024)

/* RECEBE E ANALISA EVENTO */
void get_event (int file, int fd, const char * target)
{
   /* VARIAVEIS AUXILIARES */
   ssize_t len, i = 0;
   char buff[BUFF_SIZE] = {0};
   char str[LINHA_SIZE];

   /* LE EVENTO */
   len = read (fd, buff, BUFF_SIZE);
   
   /* PROCESSA EVENTO */
   while (i < len) {
      struct inotify_event *pevent = (struct inotify_event *)&buff[i];

      char action[81+FILENAME_MAX] = {0};

      if (pevent->len) 
         strcpy (action, pevent->name);
      else
         strcpy (action, target);

      /* SE FOR MODIFICACAO NO FICHEIRO */
      if (pevent->mask & IN_MODIFY){
         if(read(file,str,LINHA_SIZE)){

            int i=0;
            int j=nrwords-1;
            /* VERIFICA SE CONTEM ALGUMA STRING */
            while (j>=0 && i==0){
               if(strstr(str,words[j].word)!=NULL)
                  i++;
                  j--;
            }               

            /* SE CONTEM, ESCREVE NO PIPE E NOTIFICA MONITOR SE ESTE ESTIVER ABERTO */
            if (i>0){
               strcat(towrite,"INFO: ");
               strcat(towrite,"O ficheiro monitorizado (");
               strcat(towrite,action);
               strcat(towrite,") foi alterado, adicionada na linha ");
               
               char buf[5];
               sprintf(buf,"%d",nrlinha);
               
               strcat(towrite,buf);
               strcat(towrite," a seguinte string :\n");
               strcat(towrite,str);

               write(filetowrite,towrite,sizeof(towrite));

               if(pidmonitor!=0){
                  kill(pidmonitor, SIGUSR1);
               }
            }
            /* SE NAO CONTIVER ESCREVE NAS STRINGS NAO MONITORIZADAS */
            else{
               NotMonitorized nm = (NotMonitorized) malloc (sizeof(struct sNotMonitorized));

               strcpy(nm->line,str);
               gettimeofday(&nm->time, 0);
               nm->linha = nrlinha;
               nm->next = nmlist;
               nmlist = nm;
            }
            memset(str,'\0',LINHA_SIZE);
            memset(towrite,'\0',LINHA_SIZE);            
            nrlinha++;
         }
      }     

      /* OUTROS EVENTOS */
      if (pevent->mask & IN_ACCESS) 
         strcat(action, " was read");
      if (pevent->mask & IN_ATTRIB) 
         strcat(action, " Metadata changed");
      if (pevent->mask & IN_CLOSE_WRITE) 
         strcat(action, " opened for writing was closed");
      if (pevent->mask & IN_CLOSE_NOWRITE) 
         strcat(action, " not opened for writing was closed");
      if (pevent->mask & IN_CREATE) 
         strcat(action, " created in watched directory");
      if (pevent->mask & IN_DELETE) 
         strcat(action, " deleted from watched directory");
      if (pevent->mask & IN_DELETE_SELF) 
         strcat(action, "Watched file/directory was itself deleted");
      if (pevent->mask & IN_MOVE_SELF) 
         strcat(action, "Watched file/directory was itself moved");
      if (pevent->mask & IN_MOVED_FROM) 
         strcat(action, " moved out of watched directory");
      if (pevent->mask & IN_MOVED_TO) 
         strcat(action, " moved into watched directory");
      if (pevent->mask & IN_OPEN) 
         strcat(action, " was opened");

      /* AVANCA */
      i += sizeof(struct inotify_event) + pevent->len;
   }

}


/* REPORTA ERROS */
void handle_error (int error)
{
   fprintf (stderr, "Error: %s\n", strerror(error));

}  


