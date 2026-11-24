#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <pthread.h>
#include <errno.h>

#define PAGE_SIZE 256
#define PAGE_COUNT 256
#define FRAME_COUNT 128
#define TLB_SIZE 16

typedef struct {
    int frame;
    int valid;
    unsigned long last_used;
} PageEntry;

typedef struct {
    int page;
    int frame;
    int valid;
    unsigned long last_used;
} TLBEntry;

PageEntry page_table[PAGE_COUNT];
TLBEntry tlb[TLB_SIZE];
signed char physical_memory[FRAME_COUNT][PAGE_SIZE];

int next_free_frame = 0;
int next_fifo_frame = 0;
unsigned long page_faults = 0;
unsigned long translated_addresses = 0;
unsigned long tlb_hits = 0;
int next_tlb_entry = 0;

unsigned long access_counter = 0;

void initialize_memory(void) {
    for (int i = 0; i < PAGE_COUNT; i++) {
        page_table[i].frame = -1;
        page_table[i].valid = 0;
        page_table[i].last_used = 0;
    }

    for (int i = 0; i < TLB_SIZE; i++) {
        tlb[i].page = -1;
        tlb[i].frame = -1;
        tlb[i].valid = 0;
        tlb[i].last_used = 0;
    }

    memset(physical_memory, 0, sizeof(physical_memory));
    next_free_frame = 0;

    page_faults = 0;
    translated_addresses = 0;
    next_fifo_frame = 0;
    access_counter = 0;
    tlb_hits = 0;
    next_tlb_entry = 0;
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
        perror("Erro ao posicionar BACKING_STORE.bin");
        return -1;
    }

    unsigned char buffer[PAGE_SIZE];

    if (fread(buffer, 1, PAGE_SIZE, backing_store) != PAGE_SIZE) {
        fprintf(stderr, "Erro: falha ao ler a pagina %d de BACKING_STORE.bin.\n", page);
        return -1;
    }

    for (int i = 0; i < PAGE_COUNT; i++) {
        if (page_table[i].valid && page_table[i].frame == frame) {
            page_table[i].valid = 0;
            page_table[i].frame = -1;

            for (int j = 0; j < TLB_SIZE; j++) {
                if (tlb[j].valid && tlb[j].page == i) {
                    tlb[j].valid = 0;
                }
            }

            break;
        }
    }

    memcpy(physical_memory[frame], buffer, PAGE_SIZE);

    page_table[page].frame = frame;
    page_table[page].valid = 1;
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

typedef struct {
    int page;
    int index;
    int found;
} TLBSearchArgs;

void *search_tlb_entry(void *arg) {
    TLBSearchArgs *data = (TLBSearchArgs *)arg;

    int index = data->index;

    if (tlb[index].valid && tlb[index].page == data->page) {
        data->found = 1;
    } else {
        data->found = 0;
    }

    return NULL;
}

int search_tlb(int page) {
    pthread_t threads[TLB_SIZE];
    // Os argumentos permanecem validos mesmo se pthread_join falhar.
    static TLBSearchArgs args[TLB_SIZE];
    int created = 0;

    for (; created < TLB_SIZE; created++) {
        args[created] = (TLBSearchArgs){ .page = page, .index = created };
        int error = pthread_create(&threads[created], NULL,
                                   search_tlb_entry, &args[created]);

        if (error != 0) {
            fprintf(stderr, "Erro ao criar thread da TLB: %s.\n", strerror(error));
            break;
        }
    }

    int failed = created != TLB_SIZE;

    for (int i = 0; i < created; i++) {
        int error = pthread_join(threads[i], NULL);
        if (error != 0) {
            fprintf(stderr, "Erro ao aguardar thread da TLB: %s.\n", strerror(error));
            failed = 1;
        }
    }

    if (failed) {
        return -2;
    }

    for (int i = 0; i < TLB_SIZE; i++) {
        if (args[i].found) {
            return i;
        }
    }

    return -1;
}

int select_lru_tlb(void) {
    int index = -1;
    unsigned long oldest_access = ULONG_MAX;

    for (int i = 0; i < TLB_SIZE; i++) {
        if (!tlb[i].valid) {
            return i;
        }

        if (tlb[i].last_used < oldest_access) {
            oldest_access = tlb[i].last_used;
            index = i;
        }
    }

    return index;
}

int insert_tlb(int page, int frame, const char *algorithm) {
    int index;

    if (strcmp(algorithm, "fifo") == 0) {
        index = next_tlb_entry;
        next_tlb_entry = (next_tlb_entry + 1) % TLB_SIZE;
    } else {
        index = select_lru_tlb();
    }

    tlb[index].page = page;
    tlb[index].frame = frame;
    tlb[index].valid = 1;
    tlb[index].last_used = access_counter;

    return index;
}

int process_address(FILE *backing_store, FILE *output, int virtual_address, const char *page_algorithm, const char *tlb_algorithm) {
    int page = get_page_number(virtual_address);
    int frame;
    int tlb_index;

    if (access_counter == ULONG_MAX - 1) {
        fprintf(stderr, "Erro: limite de enderecos processados atingido.\n");
        return -1;
    }
    access_counter++;
    tlb_index = search_tlb(page);

    if (tlb_index == -2) {
        return -1;
    }

    if (tlb_index != -1) {
        frame = tlb[tlb_index].frame;

        if (page_table[page].valid &&
            page_table[page].frame == frame) {
            tlb_hits++;
            tlb[tlb_index].last_used = access_counter;
        } else {
            tlb[tlb_index].valid = 0;
            tlb_index = -1;
        }
    }

    if (tlb_index == -1) {
        if (page_table[page].valid) {
            frame = page_table[page].frame;
        } else {
            frame = load_page(backing_store, page, page_algorithm);
            if (frame == -1) {
                return -1;
            }
            page_faults++;
        }
        tlb_index = insert_tlb(page, frame, tlb_algorithm);
    }
    page_table[page].last_used = access_counter;

    int offset = get_offset(virtual_address);
    int physical_address = frame * PAGE_SIZE + offset;
    int value = physical_memory[frame][offset];

    if (fprintf(output, "Virtual address: %d TLB: %d Physical address: %d Value: %d\n",
                virtual_address, tlb_index, physical_address, value) < 0) {
        fprintf(stderr, "Erro: falha ao escrever em correct.txt.\n");
        return -1;
    }
    printf("Virtual address: %d TLB: %d Physical address: %d Value: %d\n",
       virtual_address, tlb_index, physical_address, value);
    translated_addresses++;
    return 0;
}

// Retorna 1 para um endereco, 0 no fim do arquivo ou -1 em caso de erro.
static int read_address(FILE *input, int *address) {
    char line[256];
    size_t length = 0;
    int ch;

    while ((ch = fgetc(input)) != EOF && ch != '\n') {
        if (ch == '\0') {
            fprintf(stderr, "Erro: caractere invalido no arquivo de enderecos.\n");
            return -1;
        }
        if (length == sizeof(line) - 1) {
            fprintf(stderr, "Erro: linha muito longa no arquivo de enderecos.\n");
            return -1;
        }
        line[length++] = (char)ch;
    }

    if (ferror(input)) {
        fprintf(stderr, "Erro: falha na leitura do arquivo de enderecos.\n");
        return -1;
    }
    if (ch == EOF && length == 0) {
        return 0;
    }

    line[length] = '\0';
    char *end;
    errno = 0;
    long value = strtol(line, &end, 10);

    if (errno == ERANGE || end == line || value < 0 ||
        value >= PAGE_COUNT * PAGE_SIZE) {
        fprintf(stderr, "Erro: endereco invalido.\n");
        return -1;
    }

    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        fprintf(stderr, "Erro: formato invalido no arquivo de enderecos.\n");
        return -1;
    }

    *address = (int)value;
    return 1;
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

    FILE *input = fopen(argv[1], "r");
    if (input == NULL) {
        perror("Erro ao abrir o arquivo de enderecos");
        return EXIT_FAILURE;
    }

    FILE *backing_store = NULL;
    FILE *output = NULL;
    int exit_status = EXIT_FAILURE;
    int address;
    int read_status = read_address(input, &address);

    if (read_status == 0) {
        fprintf(stderr, "Erro: arquivo de enderecos vazio.\n");
        goto cleanup;
    }

    while (read_status > 0) {
        read_status = read_address(input, &address);
    }
    if (read_status < 0) {
        goto cleanup;
    }
    if (fseek(input, 0, SEEK_SET) != 0) {
        perror("Erro ao reposicionar o arquivo de enderecos");
        goto cleanup;
    }

    backing_store = fopen("BACKING_STORE.bin", "rb");
    if (backing_store == NULL) {
        perror("Erro ao abrir BACKING_STORE.bin");
        goto cleanup;
    }

    output = fopen("correct.txt", "w");
    if (output == NULL) {
        perror("Erro ao criar correct.txt");
        goto cleanup;
    }

    initialize_memory();

    while ((read_status = read_address(input, &address)) > 0) {
        if (process_address(backing_store, output, address, argv[2], argv[3]) < 0) {
            goto cleanup;
        }
    }
    if (read_status < 0) {
        goto cleanup;
    }
    if (translated_addresses == 0) {
        fprintf(stderr, "Erro: arquivo de enderecos vazio.\n");
        goto cleanup;
    }

    double page_fault_rate = (double)page_faults / translated_addresses;
    double tlb_hit_rate = (double)tlb_hits / translated_addresses;

    if (fprintf(output,
                "Number of Translated Addresses = %lu\n"
                "Page Faults = %lu\n"
                "Page Fault Rate = %.3f\n"
                "TLB Hits = %lu\n"
                "TLB Hit Rate = %.3f\n",
                translated_addresses, page_faults, page_fault_rate,
                tlb_hits, tlb_hit_rate) < 0) {
        fprintf(stderr, "Erro: falha ao escrever as estatisticas em correct.txt.\n");
        goto cleanup;
    }
    printf("Number of Translated Addresses = %lu\n"
       "Page Faults = %lu\n"
       "Page Fault Rate = %.3f\n"
       "TLB Hits = %lu\n"
       "TLB Hit Rate = %.3f\n",
       translated_addresses, page_faults, page_fault_rate,
       tlb_hits, tlb_hit_rate);
    exit_status = EXIT_SUCCESS;

cleanup:
    if (output != NULL && fclose(output) != 0) {
        perror("Erro ao fechar correct.txt");
        exit_status = EXIT_FAILURE;
    }
    if (backing_store != NULL && fclose(backing_store) != 0) {
        perror("Erro ao fechar BACKING_STORE.bin");
        exit_status = EXIT_FAILURE;
    }
    if (fclose(input) != 0) {
        perror("Erro ao fechar o arquivo de enderecos");
        exit_status = EXIT_FAILURE;
    }

    return exit_status;
}
