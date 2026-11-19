
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


#define PAGE_SIZE 256
#define PAGE_COUNT 256
#define FRAME_COUNT 128
#define TLB_SIZE 16

typedef struct {
    int frame;
    int valid;
    unsigned long last_used;
    unsigned long loaded_at;
} PageEntry;

typedef struct {
    int page;
    int frame;
    int valid;
    unsigned long last_used;
    unsigned long loaded_at;
} TLBEntry;

PageEntry page_table[PAGE_COUNT];
TLBEntry tlb[TLB_SIZE];
signed char physical_memory[FRAME_COUNT][PAGE_SIZE];


int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Erro: numero de argumentos invalido.\n");
        fprintf(stderr, "Uso: ./vm <arquivo> <fifo|lru> <fifo|lru>\n");
        return EXIT_FAILURE;
    }

    if (strcmp(argv[2], "fifo") != 0 &&
        strcmp(argv[2], "lru") != 0) {
        fprintf(stderr, "Erro: algoritmo de paginas invalido.\n");
        return EXIT_FAILURE;
    }

    if (strcmp(argv[3], "fifo") != 0 &&
        strcmp(argv[3], "lru") != 0) {
        fprintf(stderr, "Erro: algoritmo da TLB invalido.\n");
        return EXIT_FAILURE;
    }

    FILE *arquivo = fopen(argv[1], "r");

    if (arquivo == NULL) {
        fprintf(stderr, "Erro: nao foi possivel abrir o arquivo de enderecos.\n");
        return EXIT_FAILURE;
    }

    char linha[256];
    int quantidade = 0;

    while (fgets(linha, sizeof(linha), arquivo) != NULL) {
        char *inicio = linha;

        while (isspace((unsigned char)*inicio)) {
            inicio++;
        }

        if (*inicio == '\0') {
            fprintf(stderr, "Erro: endereco invalido.\n");
            fclose(arquivo);
            return EXIT_FAILURE;
        }

        char *fim;
        long endereco = strtol(inicio, &fim, 10);

        if (inicio == fim || endereco < 0 || endereco > 65535) {
            fprintf(stderr, "Erro: endereco invalido.\n");
            fclose(arquivo);
            return EXIT_FAILURE;
        }

        while (isspace((unsigned char)*fim)) {
            fim++;
        }

        if (*fim != '\0') {
            fprintf(stderr, "Erro: formato invalido no arquivo.\n");
            fclose(arquivo);
            return EXIT_FAILURE;
        }

        quantidade++;
    }

    if (ferror(arquivo) || quantidade == 0) {
        fprintf(stderr, "Erro: arquivo de enderecos invalido.\n");
        fclose(arquivo);
        return EXIT_FAILURE;
    }

    fclose(arquivo);

    return EXIT_SUCCESS;
}
