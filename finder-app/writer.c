#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage(char* invocation_name);

int main(int argc, char** argv){

  if(argc < 3){
    (void)printf("Missing parameters\n");
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  const char* writefile = argv[1];
  const char* writestr = argv[2];

  // Making the directory is ignored for the C version
  FILE *output_file = fopen(writefile, "w");
  if(output_file == NULL){
    (void)printf("Could not create/open file '%s'\n", writefile);
    return EXIT_FAILURE;
  }

  const long write_return_value = fwrite(writestr, sizeof(*writestr), strlen(writestr), output_file);
  if(write_return_value != strlen(writestr)){
    (void)printf("Could not write all data to file\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

void print_usage(char* invocation_name){
  (void)printf("Usage: %s <writefile> <writestr>\n", invocation_name);
  (void)printf("where\n");
  (void)printf("  writefile: file to write to (created if it doesn't exist)\n");
  (void)printf("  writestr: content to write (clobbers existing content)\n");
}
