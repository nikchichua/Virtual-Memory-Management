#include <math.h>
#include "my_vm.h"

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef intptr_t Int;

void *physical_memory = NULL;

uint8_t *physical_bitmap = NULL;
uint8_t *virtual_bitmap = NULL;

Int *directory;

int offset_bits;
int vpn_bits;
int max_levels;

int directory_bits;
int directory_entries;
int directory_size;

int table_bits;
int table_entries;
int table_size;

int pages_per_directory;
int pages_per_table;

Int ppn_pointer = 1;
Int vpn_pointer = 0;

double translations = 0;
double tlb_misses = 0;

Int extract(Int number, int index, int n);
Int *get_page(Int ppn);
Int get_vpn(void *virtual_address);
Int get_offset(void *virtual_address);
void *virtual_address(Int vpn, Int offset);
void set_bit(void *ptr, int index);
void unset_bit(void *ptr, int index);
bool is_bit_free(uint8_t *bitmap, int index);
int allocate_pages(uint8_t *bitmap, int start, int num_pages);

void init_tlb() {
    cache.entries = malloc(TLB_ENTRIES * sizeof(Entry));
    for(int i = 0; i < TLB_ENTRIES; i++) {
        cache.entries[i].vpn = -1;
        cache.entries[i].ppn = -1;
    }
}

void add_TLB(int vpn, int ppn) {
    for(int i = 0; i < TLB_ENTRIES; i++) {
        if(cache.entries[i].vpn == -1) {
            cache.entries[i].vpn = vpn;
            cache.entries[i].ppn = ppn;
            return;
        }
    }
    cache.entries[0].vpn = vpn;
    cache.entries[0].ppn = ppn;
}

void remove_TLB(int vpn) {
    for(int i = 0; i < TLB_ENTRIES; i++) {
        if(cache.entries[i].vpn == vpn) {
            cache.entries[i].vpn = -1;
            break;
        }
    }
}

int check_TLB(int vpn) {
    for(int i = 0; i < TLB_ENTRIES; i++) {
        if(cache.entries[i].vpn == vpn && cache.entries[i].ppn != -1) {
            return cache.entries[i].ppn;
        }
    }
    tlb_misses++;
    return -1;
}

void print_TLB_missrate() {
    double miss_rate = translations == 0 ? 0 : tlb_misses / translations;
    fprintf(stderr, "Number of total translations %lf \n", translations);
    fprintf(stderr, "Number of TLB misses %lf \n", tlb_misses);
    fprintf(stderr, "TLB miss rate %lf \n", miss_rate);
}

void set_physical_mem() {
    physical_memory = calloc(MEMSIZE, sizeof(char));
    physical_bitmap = calloc(BITMAP_SIZE, sizeof(char));
    virtual_bitmap = calloc(BITMAP_SIZE, sizeof(char));

    offset_bits = (int) log2(PGSIZE);
    vpn_bits = ADDRESS_SPACE - offset_bits;
    max_levels =  ceil((ADDRESS_SPACE - offset_bits) / log2((int)(PGSIZE / sizeof(intptr_t))));

    directory_bits  = (vpn_bits % (max_levels) == 0)
                      ? (vpn_bits / (max_levels))
                      :  vpn_bits - ((max_levels - 1) * ((vpn_bits / (max_levels)) + 1));
    directory_entries = (int) pow(2, directory_bits);
    directory_size = directory_entries * (int) sizeof(intptr_t);

    table_bits = (vpn_bits % (max_levels) == 0) ? (vpn_bits / (max_levels)) : (vpn_bits / (max_levels)) + 1;
    table_entries = (int) pow(2, table_bits);
    table_size = directory_entries * (int) sizeof(intptr_t);

    pages_per_directory = directory_size / PGSIZE;
    pages_per_directory = (1 > pages_per_directory) ? 1 : pages_per_directory;
    pages_per_table = table_size / PGSIZE;
    pages_per_table = (1 > pages_per_table) ? 1 : pages_per_table;
}

void init_directory() {
    directory = physical_memory;
    for(int i = 0; i < directory_entries; i++) {
        directory[i] = -1;
    }
    for(int i = 0; i < pages_per_directory; i++) {
        set_bit(physical_bitmap, i);
    }
}

void init_page_table(Int ppn) {
    Int *page = (Int *) &((uint8_t *)physical_memory)[ppn * PGSIZE];
    for(int i = 0; i < table_entries; i++) {
        *page = -1;
        page++;
    }
}

Int translate_page(Int vpn) {
    translations++;
    int tlb_ppn = check_TLB((int)vpn);
    if(tlb_ppn != -1) return tlb_ppn;
    Int *ppn;
    Int *table;
    int each_level;
    for (each_level = max_levels - 1; each_level >= 0; each_level--) {
        int bits = (each_level == max_levels - 1) ? directory_bits : table_bits;
        Int index = extract(vpn, each_level * table_bits, bits);
        ppn = (each_level == max_levels - 1) ? &directory[index] : &table[index];
        table = get_page(*ppn);
    }
    return *ppn;
}

Int map_page(Int vpn) {
    Int converted_vpn = vpn - 1;
    Int *ppn;
    Int *table;
    int each_level;
    for (each_level = max_levels - 1; each_level >= 0; each_level--) {
        int bits = (each_level == max_levels - 1) ? directory_bits : table_bits;
        Int index = extract(converted_vpn, each_level * table_bits, bits);
        ppn = (each_level == max_levels - 1) ? &directory[index] : &table[index];
        if (*ppn == -1) {
            *ppn = allocate_pages(physical_bitmap, (int) ppn_pointer, pages_per_table);
            if (*ppn == -1) return -1;
            init_page_table(*ppn);
        }
        ppn_pointer = *ppn + 1;
        table = get_page(*ppn);
    }
    add_TLB((int) converted_vpn, (int) *ppn);
    return *ppn;
}

void *t_malloc(unsigned int num_bytes) {
    pthread_mutex_lock(&lock);
    if(physical_memory == NULL) {
        set_physical_mem();
        init_directory();
        init_tlb();
    }
    int num_pages = (int) ceil((double) num_bytes / PGSIZE);
    int vpn = allocate_pages(virtual_bitmap, (int) vpn_pointer, num_pages) + 1;
    if(vpn == -1) {
        printf("Allocation error: not enough contiguous virtual memory!\n");
        pthread_mutex_unlock(&lock);
        return NULL;
    }
    int each_vpn = vpn;
    for (int i = 0; num_bytes > 0; i++) {
        uint chunk = (num_bytes >= PGSIZE) ? PGSIZE : num_bytes;
        if(map_page(each_vpn) == -1) {
            printf("Allocation error: not enough physical memory!\n");
            pthread_mutex_unlock(&lock);
            return NULL;
        }
        each_vpn++;
        num_bytes -= chunk;
    }
    void *address = virtual_address(vpn, 0);
    pthread_mutex_unlock(&lock);
    return address;
}

bool clean_tables(Int *page, int level) {
    if(level <= 0) {
        return false;
    }
    int entries = (level == max_levels) ? directory_entries : table_entries;
    bool is_empty = true;
    for(int i = 0; i < entries; i++) {
        if(page[i] != -1) {
            is_empty = false;
            Int *deeper_page = get_page(page[i]);
            bool empty = clean_tables(deeper_page, level - 1);
            if(empty) {
                unset_bit(physical_bitmap, (int) page[i]);
                ppn_pointer = 1;
                page[i] = -1;
                is_empty = true;
            }
        }
    }
    return is_empty;
}

void unmap_page(Int vpn) {
    remove_TLB((int)vpn);
    Int *ppn;
    Int *table;
    int each_level;
    for (each_level = max_levels - 1; each_level >= 0; each_level--) {
        Int index = extract(vpn, each_level * table_bits, (each_level == max_levels - 1) ? directory_bits : table_bits);
        ppn = (each_level == max_levels - 1) ? &directory[index] : &table[index];
        table = get_page(*ppn);
    }
    unset_bit(physical_bitmap, (int) *ppn);
    unset_bit(virtual_bitmap, (int) vpn);
    ppn_pointer = *ppn;
    *ppn = -1;
}

bool is_allocated_contiguously(Int vpn, int num_pages) {
    for(int i = 0; i < num_pages; i++) {
        if(vpn >= PAGE_AMOUNT) return false;
        if(is_bit_free(virtual_bitmap, (int) vpn)) {
            return false;
        }
        vpn++;
    }
    return true;
}

void t_free(void *va, int num_bytes) {
    pthread_mutex_lock(&lock);
    if(num_bytes <= 0) return;
    int num_pages = (int) ceil((double) num_bytes / PGSIZE);
    Int vpn = get_vpn(va);
    if(!is_allocated_contiguously(vpn, num_pages)) {
        printf("Deallocation error: attempted to free non-contiguously allocated virtual memory!\n");
        pthread_mutex_unlock(&lock);
        return;
    }
    vpn_pointer = (vpn_pointer < vpn) ? vpn_pointer : vpn;
    Int each_vpn = vpn;
    for (int i = 0; num_bytes > 0; i++) {
        int chunk = (num_bytes >= PGSIZE) ? PGSIZE : num_bytes;
        unmap_page(each_vpn);
        each_vpn++;
        num_bytes -= chunk;
    }
    clean_tables(directory, max_levels);
    pthread_mutex_unlock(&lock);
}

int put_value(void *va, void *val, int num_bytes) {
    pthread_mutex_lock(&lock);
    if(num_bytes <= 0 || val == NULL) return -1;
    int num_pages = (int) ceil((double) num_bytes / PGSIZE);
    Int vpn = get_vpn(va);
    Int offset = get_offset(va);
    if(!is_allocated_contiguously(vpn, num_pages)) {
        printf("Writing error: attempted to write values into non-contiguously allocated memory!\n");
        pthread_mutex_unlock(&lock);
        return -1;
    }
    Int each_vpn = vpn;
    for (int i = 0; num_bytes > 0; i++) {
        int chunk = (num_bytes >= PGSIZE) ? PGSIZE : num_bytes;
        Int each_ppn = translate_page(each_vpn);
        Int *each_page = get_page(each_ppn);
        if(i == 0) each_page = (Int *) (((int8_t *) each_page) + offset);
        memcpy(each_page, val, chunk);
        val += chunk;
        each_vpn++;
        num_bytes -= chunk;
    }
    pthread_mutex_unlock(&lock);
    return 0;
}

void get_value(void *va, void *val, int num_bytes) {
    pthread_mutex_lock(&lock);
    if(num_bytes <= 0 || val == NULL) return;
    int num_pages = (int) ceil((double) num_bytes / PGSIZE);
    Int vpn = get_vpn(va);
    Int offset = get_offset(va);
    if(!is_allocated_contiguously(vpn, num_pages)) {
        printf("Reading error: attempted to read values from non-contiguously allocated memory!\n");
        pthread_mutex_unlock(&lock);
        return;
    }
    Int each_vpn = vpn;
    for (int i = 0; num_bytes > 0; i++) {
        int chunk = (num_bytes >= PGSIZE) ? PGSIZE : num_bytes;
        Int each_ppn = translate_page(each_vpn);
        Int *each_page = get_page(each_ppn);
        if(i == 0) each_page = (Int *) (((int8_t *) each_page) + offset);
        memcpy(val, each_page, chunk);
        val += chunk;
        each_vpn++;
        num_bytes -= chunk;
    }
    pthread_mutex_unlock(&lock);
}

void mat_mult(void *mat1, void *mat2, int size, void *answer) {
    int i, j, k;
    for (i = 0; i < size; i++) {
        for(j = 0; j < size; j++) {
            unsigned int a, b, c = 0;
            for (k = 0; k < size; k++) {
                uint64_t address_a = (uint64_t)mat1 + ((i * size * sizeof(int))) + (k * sizeof(int));
                uint64_t address_b = (uint64_t)mat2 + ((k * size * sizeof(int))) + (j * sizeof(int));
                get_value( (void *)address_a, &a, sizeof(int));
                get_value( (void *)address_b, &b, sizeof(int));
                c += (a * b);
            }
            uint64_t address_c = (uint64_t)answer + ((i * size * sizeof(int))) + (j * sizeof(int));
            put_value((void *)address_c, (void *)&c, sizeof(int));
        }
    }
}

Int extract(Int number, int index, int n) {
    int bitness = sizeof(intptr_t) * 8;
    if (n <= 0 || index < 0 || index >= bitness || n + index > bitness) {
        return 0;
    }
    Int shifted_number = number >> index;
    Int mask = ((Int)1 << n) - 1;
    Int result = shifted_number & mask;
    return result;
}

Int *get_page(Int ppn) {
    return (Int *) &((uint8_t *)physical_memory)[ppn * PGSIZE];
}

Int get_vpn(void *virtual_address) {
    Int address = (Int) virtual_address;
    return (address >> offset_bits) - 1;
}

Int get_offset(void *virtual_address) {
    Int num = (Int) virtual_address;
    Int mask = (1 << offset_bits) - 1;
    return num & mask;
}

void *virtual_address(Int vpn, Int offset) {
    Int result = vpn << offset_bits;
    offset &= (~((~0ll) << (offset_bits)));
    result |= offset;
    return (void *) result;
}

void set_bit(void *ptr, int index) {
    uint8_t *bitmap = ptr;
    int bitmap_idx = index / 8;
    int bit_idx = index % 8;
    uint8_t *target = &bitmap[bitmap_idx];
    uint8_t mask = 1 << (7 - bit_idx);
    *target |= mask;
}

void unset_bit(void *ptr, int index) {
    uint8_t *bitmap = ptr;
    int bitmap_idx = index / 8;
    int bit_idx = index % 8;
    uint8_t *target = &bitmap[bitmap_idx];
    uint8_t mask = 1 << (7 - bit_idx);
    *target &= ~mask;
}

int get_bit(void *ptr, int index) {
    uint8_t *bitmap = ptr;
    int bitmap_idx = index / 8;
    int bit_idx = index % 8;
    uint8_t bit = bitmap[bitmap_idx] >> (7 - bit_idx) & 1;
    return bit;
}

bool is_bit_free(uint8_t *bitmap, int index) {
    return get_bit(bitmap, index) == 0;
}

bool allocate_memory_helper(uint8_t *bitmap, int start, int i, int num_bits, int *free_bytes) {
    if (i >= num_bits) {
        return true;
    }
    if (is_bit_free(bitmap, start)) {
        if(free_bytes != NULL) *free_bytes += 1;
    } else {
        return false;
    }
    bool allocated = allocate_memory_helper(bitmap, start + 1, i + 1, num_bits, free_bytes);
    if (allocated) {
        set_bit(bitmap, start);
        return true;
    }
    return false;
}

bool allocate_memory(uint8_t *bitmap, int start, int num_bits, int *free_bytes) {
    if(num_bits == 0) {
        return true;
    }
    return allocate_memory_helper(bitmap, start, 0, num_bits, free_bytes);
}

int allocate_pages(uint8_t *bitmap, int start, int num_pages) {
    if(num_pages <= 0) {
        return -1;
    }
    for(int i = start; i < PAGE_AMOUNT; i++) {
        if(i + num_pages > PAGE_AMOUNT) {
            return -1;
        }
        if(allocate_memory(bitmap, i, num_pages, NULL)) {
            return i;
        }
    }
    return -1;
}