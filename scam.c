#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "fs.h"

#define SWAP_SIZE SWAPBLOCKS
#define SWAP_SLOT SWAP_SIZE/8
#define SWAP_START 2


uint rmap[PHYSTOP>>PTXSHIFT];
pte_t* proc_ref[PHYSTOP>>PTXSHIFT][NPROC];



struct swap_slot{
  uint is_free;
  uint start; //block id of start
  uint reference_count;
  uint page_perm;
  pte_t* processReference[NPROC];
};

struct swap_slot swap_array[SWAP_SLOT];

void incrementRefC(uint pa, pte_t* pte){
  proc_ref[pa>>PTXSHIFT][rmap[pa >> PTXSHIFT]] = pte;
  rmap[pa >> PTXSHIFT]++;
}

void decrementRefC(uint pa, pte_t* pte){
  int idx = -1;
  for (int i=0; i<rmap[pa >> PTXSHIFT]; i++){
    if (proc_ref[pa >> PTXSHIFT][i] == pte){
      idx = i;
    // iska confirm karna hoga //
    //   break;
    }
  }
  if (idx == -1){
    panic("decrementRefC: page not found");
  }
  rmap[pa >> PTXSHIFT]--;
  for (int i=idx; i<rmap[pa >> PTXSHIFT]; i++){
    proc_ref[pa >> PTXSHIFT][i] = proc_ref[pa >> PTXSHIFT][i+1];
  }
}


void save_process_reference(uint pa, uint slot_no){
    swap_array[slot_no].reference_count = rmap[pa >> PTXSHIFT];
    for(int i=0;i<rmap[pa >> PTXSHIFT];++i){
        pte_t* pte = proc_ref[pa >> PTXSHIFT][i];
        swap_array[slot_no].processReference[i] = pte;
        int block = (slot_no * 8)+2;
        *pte = (block << PTXSHIFT) | PTE_SP | PTE_FLAGS(*pte);
        *pte &= ~PTE_P;
    }
    rmap[pa >> PTXSHIFT] = 0;
}


uint getRefC(uint pa){
  return rmap[pa >> PTXSHIFT];
}


void setRmap(uint pa){
  rmap[pa >> PTXSHIFT] = 0;
}

static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}


void restore_process_reference(uint pa, uint slot_no){
    rmap[pa >> PTXSHIFT] = swap_array[slot_no].reference_count;
    for (int i=0;i<rmap[pa >> PTXSHIFT];++i){
        pte_t* pte = swap_array[slot_no].processReference[i];
        *pte = pa | PTE_FLAGS(*pte) | PTE_P;
        *pte &= ~PTE_SP;
        proc_ref[pa >> PTXSHIFT][i] = pte;
    }
}


// ((P)) id: 1, state: 2, rss: 12288
// ((P)) id: 2, state: 2, rss: 20480
// ((P)) id: 3, state: 4, rss: 1433600
// ((P)) id: 4, state: 2, rss: 974848
// Child alloc-ed
// PrintingRSS
// ((P)) id: 1, state: 2, rss: 12288
// ((P)) id: 2, state: 2, rss: 20480
// ((P)) id: 3, state: 2, rss: 1187840
// ((P)) id: 4, state: 4, rss: 1925120


// PrintingRSS
// ((P)) id: 1, state: 2, rss: 12288
// ((P)) id: 2, state: 2, rss: 20480
// ((P)) id: 3, state: 4, rss: 1433600
// ((P)) id: 4, state: 2, rss: 974848
// Child alloc-ed
// PrintingRSS
// ((P)) id: 1, state: 2, rss: 12288
// ((P)) id: 2, state: 2, rss: 20480
// ((P)) id: 3, state: 2, rss: 1433600
// ((P)) id: 4, state: 4, rss: 2416640


void rssSahiKaroBC(uint pa, int SIZE){
    struct proc* mani;
    uint i;
    for(int j=0; j<NPROC; j++){
        mani = get_ptable_proc(j);
        if(mani->state == UNUSED) continue;
        uint sz = mani->sz;
        pde_t* pgdir = mani->pgdir;
        for(i = 0; i < sz; i += PGSIZE){
            pte_t* pte = walkpgdir(pgdir, (char*)i, 0);
            if (*pte & PTE_P){
                if (PTE_ADDR(*pte) == pa){
                    mani->rss += SIZE;
                }
            }
        }
    }
}




void swap_space_init(){
    for(int i=0;i<SWAP_SLOT;++i){
        swap_array[i].is_free = 1;
        swap_array[i].start = SWAP_START + i * 8;
    }
}




pte_t* select_a_victim_page(struct proc* v_process){
    pde_t* pgdir = v_process->pgdir;
    for(int i=0;i<NPDENTRIES;i++){
        if(!(pgdir[i] & PTE_P)){
            continue;
        }
        pte_t* pgtab = (pte_t*)P2V(PTE_ADDR(pgdir[i]));
        for(int j=0;j<NPTENTRIES;j++){
            if((pgtab[j]&PTE_P)&& (pgtab[j]&PTE_U)){
                if(!(pgtab[j]&PTE_A)){
                    return &pgtab[j];
                }
            }
        }
    }
    
    int count=0;
    for(int i=0;i<NPDENTRIES;i++){
        if (!(pgdir[i] & PTE_P)) continue;
        pte_t* pgtab = (pte_t*)P2V(PTE_ADDR(pgdir[i]));
        for(int j=0;j<NPTENTRIES;j++){
            if((pgtab[j]&PTE_P)&& (pgtab[j]&PTE_U) && (pgtab[j] & PTE_A)){
                if(count%10==0) pgtab[j] &= ~PTE_A;
            count++;
            }
        }
    }
    return select_a_victim_page(v_process);
}


int get_swap_slot(){
    for(int i=0;i<SWAP_SLOT;++i){
        if(swap_array[i].is_free == 1){
            swap_array[i].is_free = 0;
            return i;
        }
    }
    return -1;
}

void page_bahar_nikaloo(pte_t *pte){
    int slot_number = get_swap_slot();
    if(slot_number==-1){
        panic("no swap slot available");
    }
    uint physical_address = PTE_ADDR(*pte);
    char* victimAddr = (char*)P2V(physical_address);
    write_page_to_disk(ROOTDEV, victimAddr, swap_array[slot_number].start);
    // swap_array[slot_number].page_perm = PTE_FLAGS(*pte);
    // *pte = PTE_FLAGS(*pte) | (swap_array[slot_number].start << PTXSHIFT);
    // *pte = *pte & ~PTE_P;
    // *pte |= PTE_SP;
    save_process_reference(V2P(victimAddr), slot_number);
    kfree(victimAddr);
}

// swap page out by finding victim page and swapping that page
void swap_page(){
    struct proc* p = select_victim_process();
    pte_t* pte = select_a_victim_page(p);
    // p->rss -= PGSIZE;
    rssSahiKaroBC(PTE_ADDR(*pte), -PGSIZE);
    page_bahar_nikaloo(pte);
}

void swapped_page_handler(pte_t* fault_page){
    // p->rss += PGSIZE;
    int block = (*fault_page) >> PTXSHIFT;
    int swap_idx = (block - 2) / 8;
    char* new_page_addr = kalloc();
    rssSahiKaroBC(V2P(new_page_addr), PGSIZE);
    read_page_from_disk(ROOTDEV, new_page_addr, block);
    restore_process_reference(V2P(new_page_addr), swap_idx);
    // uint perm = swap_array[swap_idx].page_perm;
    // *fault_page = V2P(new_page_addr) | perm;
    // *fault_page |= PTE_P;
    swap_array[swap_idx].is_free = 1;
}


void writeable_page_handler(pte_t* fault_page){
    uint pa = PTE_ADDR(*fault_page);
    uint flags = PTE_FLAGS(*fault_page);
    char *mem;
    int refCount = getRefC(pa);
    if (refCount > 1){
        if ((mem = kalloc()) == 0){
            panic("page_fault_handler: kalloc failed");
        }
        incrementRefC(V2P(mem), fault_page);
        memmove(mem, (char*)P2V(pa), PGSIZE);
        *fault_page = V2P(mem) | PTE_W | flags;
        lcr3(V2P(myproc()->pgdir));
        decrementRefC(pa, fault_page);
    }
    else{
        *fault_page |= PTE_W;
    }
}


void page_fault_handler(){
    struct proc* p = myproc();
    uint fault_addr = rcr2();
    pte_t* fault_page = walkpgdir(p->pgdir, (char*)fault_addr, 0);
    // iska confirm karna hoga //
    // conditionds for here 
    if ((*fault_page & PTE_SP)){
        swapped_page_handler(fault_page);
    }
    else if (!(*fault_page & PTE_W)){
        writeable_page_handler(fault_page);
    }
    else {
        cprintf(*fault_page | PTE_W ? "write fault\n" : "no write fault\n");
        cprintf(*fault_page | PTE_P ? "present fault\n" : "no present fault\n");
        cprintf("pid: %d, fault_addr: %x\n", p->pid, fault_addr);
        panic("scam: page_fault_handler : no succh case should be there");
    }
}


void clear_swap_slot(int slot, pte_t* pte){
    /// remove this pte and if ref count after removal is zero then free the slot
    int idx = -1;
    for(int i=0;i<swap_array[slot].reference_count;++i){
        if(swap_array[slot].processReference[i] == pte){
            idx = i;
            break;
        }
    }
    if(idx == -1){
       panic("scam.c: clear_swap_slot: page not found in swap slot");
    }
    swap_array[slot].reference_count--;
    for(int i=idx;i<swap_array[slot].reference_count;++i){
        swap_array[slot].processReference[i] = swap_array[slot].processReference[i+1];
    }
    if(swap_array[slot].reference_count == 0){
        swap_array[slot].is_free = 1;
    }
}


void swap_table_me_daalo(int swap_idx , pte_t* pte){
    swap_array[swap_idx].processReference[swap_array[swap_idx].reference_count] = pte;
    swap_array[swap_idx].reference_count++;
}
