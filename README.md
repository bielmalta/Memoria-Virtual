# Memória Virtual

Implementação de um gerenciador de memória virtual em C para a disciplina de Infraestrutura de Software.
O programa realiza a tradução de endereços virtuais para físicos, utilizando uma memória física de 128 frames, tabela de páginas e TLB.

## Funcionalidades

- Tradução de endereços virtuais para físicos.
- Substituição de páginas pelos algoritmos FIFO e LRU.
- Substituição de entradas da TLB pelos algoritmos FIFO e LRU.
- Busca na TLB utilizando threads com pthread.
- Contabilização de Page Faults e TLB Hits.
- Tratamento de erros de entrada e execução.

## Compilação

Para compilar o programa:

```bash
make
```

Para remover os arquivos compilados:

```bash
make clean
```

## Execução

```bash
./vm addresses.txt lru fifo
```

Argumentos:

1. Arquivo contendo os endereços virtuais.
2. Algoritmo de substituição de páginas (`fifo` ou `lru`).
3. Algoritmo de substituição da TLB (`fifo` ou `lru`).

O arquivo `BACKING_STORE.bin` deve estar no diretório de execução.

## Saída

O programa gera o arquivo `correct.txt`, contendo os endereços traduzidos, os valores encontrados na memória e as estatísticas de Page Faults e TLB Hits.