#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

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


int next_free_frame = 0;
int next_fifo_frame = 0;
int page_faults = 0;
int translated_addresses = 0;

unsigned long access_counter = 0;


void initialize_memory(void) {
    for (int i = 0; i < PAGE_COUNT; i++) {
        page_table[i].frame = -1;
        page_table[i].valid = 0;
        page_table[i].last_used = 0;
        page_table[i].loaded_at = 0;
    }

    for (int i = 0; i < TLB_SIZE; i++) {
        tlb[i].page = -1;
        tlb[i].frame = -1;
        tlb[i].valid = 0;
        tlb[i].last_used = 0;
        tlb[i].loaded_at = 0;
    }

    memset(physical_memory, 0, sizeof(physical_memory));
    next_free_frame = 0;

    page_faults = 0;
    translated_addresses = 0;
    next_fifo_frame = 0;
    access_counter = 0;
}


int select_lru_frame(void) {
    unsigned long oldest_access = ULONG_MAX;
    int selected_frame = -1;

    for (int i = 0; i < PAGE_COUNT; i++) {
        if (page_table[i].valid &&
            page_table[i].last_used < oldest_access) {

            oldest_access = page_table[i].last_used;
            selected_frame = page_table[i].frame;
        }
    }

    return selected_frame;
}



int load_page(FILE *backing_store, int page, const char *algorithm) {
    int frame;

    if (next_free_frame < FRAME_COUNT) {
        frame = next_free_frame;
    } else {
        if (strcmp(algorithm, "fifo") == 0) {
            frame = next_fifo_frame;
            next_fifo_frame = (next_fifo_frame + 1) % FRAME_COUNT;
        } else {
            frame = select_lru_frame();
        }

        if (frame == -1) {
            return -1;
        }
    }

    if (fseek(backing_store, page * PAGE_SIZE, SEEK_SET) != 0) {
        return -1;
    }

    unsigned char buffer[PAGE_SIZE];

    if (fread(buffer, 1, PAGE_SIZE, backing_store) != PAGE_SIZE) {
        return -1;
    }

    for (int i = 0; i < PAGE_COUNT; i++) {
        if (page_table[i].valid && page_table[i].frame == frame) {
            page_table[i].valid = 0;
            page_table[i].frame = -1;
            break;
        }
    }

    memcpy(physical_memory[frame], buffer, PAGE_SIZE);

    page_table[page].frame = frame;
    page_table[page].valid = 1;
    page_table[page].loaded_at = access_counter;
    page_table[page].last_used = access_counter;

    if (next_free_frame < FRAME_COUNT) {
        next_free_frame++;
    }

    return frame;
}

int get_page_number(int virtual_address) {
    return virtual_address / PAGE_SIZE;
}

int get_offset(int virtual_address) {
    return virtual_address % PAGE_SIZE;
}


int translate_address(int virtual_address, int frame) {
    int offset = virtual_address % PAGE_SIZE;

    return frame * PAGE_SIZE + offset;
}



int process_address(FILE *backing_store, int virtual_address, const char *algorithm) {
    int page = get_page_number(virtual_address);
    int frame;

    access_counter++;

    if (page_table[page].valid) {
        frame = page_table[page].frame;
    } else {
        frame = load_page(backing_store, page, algorithm);

        if (frame == -1) {
            return -1;
        }

        page_faults++;
    }

    page_table[page].last_used = access_counter;

    int physical_address = translate_address(virtual_address, frame);
    int offset = get_offset(virtual_address);
    int value = physical_memory[frame][offset];

    translated_addresses++;

    printf("Virtual address: %d Physical address: %d Value: %d\n",
           virtual_address, physical_address, value);

    return 0;
}



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

    
    rewind(arquivo);

    FILE *backing_store = fopen("BACKING_STORE.bin", "rb");

    if (backing_store == NULL) {
        fprintf(stderr, "Erro: nao foi possivel abrir BACKING_STORE.bin.\n");
        fclose(arquivo);
        return EXIT_FAILURE;
    }

    initialize_memory();

    while (fgets(linha, sizeof(linha), arquivo) != NULL) {
        int endereco = (int)strtol(linha, NULL, 10);

        if (process_address(backing_store, endereco, argv[2]) == -1) {
            fprintf(stderr, "Erro: memoria fisica cheia.\n");
            fclose(backing_store);
            fclose(arquivo);
            return EXIT_FAILURE;
        }
    }

    if (ferror(arquivo)) {
        fprintf(stderr, "Erro: falha na leitura dos enderecos.\n");
        fclose(backing_store);
        fclose(arquivo);
        return EXIT_FAILURE;
    }

    printf("Number of Translated Addresses = %d\n", translated_addresses);
    printf("Page Faults = %d\n", page_faults);

    fclose(backing_store);
    fclose(arquivo);

    return EXIT_SUCCESS;

}
