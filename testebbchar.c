#include<stdio.h>
#include<stdlib.h>
#include<errno.h>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>
       
int main(int argc, char *argv[]){
   int ret, fd, num=0;
   FILE * fp;
   char * line = NULL;
   size_t len = 0;
   if (argc<2)
   {printf("USAGE: sudo ./test filename\nE.G. sudo ./test sample.txt\n");return -1;}
   printf("Starting device test code example...\n");
   fd = open("/dev/ebbchar", O_RDWR);             // Open the device with read/write access
   if (fd < 0){
      perror("Failed to open the device...");
      return errno;
   }
   fp = fopen(argv[1], "r");
    if (fp == NULL)
        exit(EXIT_FAILURE);
    while ((getline(&line, &len, fp)) != -1) {
        printf("%s", line);
        ret = write(fd, line, len); // Send the string to the LKM
        if (ret < 0)
        {
            perror("Failed to write the message to the device.");
            return errno;
        }
    }
   num = read(fd, line, len);
   printf("Total lines read into kernel log : %d\n", num);

   printf("End of the program\n");
   return 0;
}
