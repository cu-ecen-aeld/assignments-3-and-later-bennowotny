#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/syslog.h>
#include <syslog.h>

void print_usage(char* invocation_name);

int main(int argc, char** argv){

  openlog("writer", 0, LOG_USER);

  if(argc < 3){
    syslog(LOG_ERR, "Missing parameters");
    (void)printf("Missing parameters\n");
    print_usage(argv[0]);
    closelog();
    return EXIT_FAILURE;
  }

  const char* writefile = argv[1];
  const char* writestr = argv[2];

  // Making the directory is ignored for the C version of writer
  FILE *output_file = fopen(writefile, "w");
  if(output_file == NULL){
    syslog(LOG_ERR, "Could not create/open file '%s'", writefile);
    (void)printf("Could not create/open file '%s'\n", writefile);
    closelog();
    return EXIT_FAILURE;
  }

  syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
  const long write_return_value = fwrite(writestr, sizeof(*writestr), strlen(writestr), output_file);
  if(write_return_value != strlen(writestr)){
    syslog(LOG_ERR, "Could not write all data to file");
    (void)printf("Could not write all data to file\n");
    closelog();
    return EXIT_FAILURE;
  }

  closelog();
  return EXIT_SUCCESS;
}

void print_usage(char* invocation_name){
  (void)printf("Usage: %s <writefile> <writestr>\n", invocation_name);
  (void)printf("where\n");
  (void)printf("  writefile: file to write to (created if it doesn't exist)\n");
  (void)printf("  writestr: content to write (clobbers existing content)\n");
}
