#include <crypt/sha256.h>
#include <tree/merkletree.h>
#include <chk/pkgchk.h>
#include <chk/pkg_helper.h>
#include <utilities/my_utils.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdio.h>


// Part 1 Source Code

/**
 * Gets only the required/min hashes to represent the current completion state
 * Return the smallest set of hashes of completed branches to represent
 * the completion state of the file.
 *
 * @param bpkg, constructed bpkg object
 * @return query_result, This structure will contain a list of hashes
 * 		and the number of hashes that have been retrieved
 */
bpkg_t* bpkg_load(const char* path)
{
    bpkg_t* bpkg = (bpkg_t*) my_malloc(sizeof(bpkg_t));

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("Cannot open file\n");
        return NULL;
    }

    struct stat statbuf;
    if (fstat(fd, &statbuf)) {
    perror("Fstat failure\n");
    close(fd);
    free(bpkg);
    return NULL;
    }

    char* bpkg_data = (char*)mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (bpkg_data == MAP_FAILED) {
        perror("Cannot mmap file\n");
        close(fd);
        free(bpkg);
        return NULL;
    }
    close(fd);

    bpkg_unpack(bpkg);

    if (mtree_build(bpkg) == NULL) {
        munmap(bpkg_data, statbuf.st_size);
        free(bpkg);
        return NULL;
    }

    munmap(bpkg_data, statbuf.st_size);
    return bpkg;
}

/**
 * Checks to see if the referenced filename in the bpkg file
 * exists or not.
 * @param bpkg, constructed bpkg object
 * @return query_result, a single string should be
 *      printable in hashes with len sized to 1.
 * 		If the file exists, hashes[0] should contain "File Exists"
 *		If the file does not exist, hashes[0] should contain "File Created"
 */
bpkg_query_t bpkg_file_check(bpkg_t* bpkg)
{
    bpkg_query_t qobj;
    mtree_t* mtree = bpkg->mtree;

    if (access(bpkg->filename, F_OK) == 0) 
    {
    qobj.hashes[0] = "File exists";
    } 
    else 
    {
        FILE* fptr = fopen(bpkg->filename, "wb");
        if (fptr == NULL) 
        {
            perror("Failed to create bpkg data file...");
            qobj.hashes[0] = "File creation failed";
            return qobj;
        }

        fseek(fptr, mtree->f_size - 1, SEEK_SET);
        fwrite("\0", 1, 1, fptr);
        fclose(fptr);
        qobj.hashes[0] = "File created";
    }

    return qobj;
}

/**
 * Retrieves a list of all hashes within the package/tree
 * @param bpkg, constructed bpkg object
 * @return query_result, This structure will contain a list of hashes
 * 		and the number of hashes that have been retrieved
 */
bpkg_query_t bpkg_get_all_hashes(bpkg_t* bpkg)
{
    char* hashes[SHA256_HEXLEN];
 
    mtree_t* mtree = bpkg->mtree;

    for (int i=0; i < mtree->nnodes; i++)
    {
        hashes[i] = mtree->nodes[i]->expected_hash;
    }

    bpkg_query_t qry = {
        .hashes = hashes,
        .len = mtree->nnodes,
    };
    
    return qry;
}

/**
 * Retrieves all completed chunks of a package object
 * @param bpkg, constructed bpkg object
 * @return query_result, This structure will contain a list of hashes
 * 		and the number of hashes that have been retrieved
 */
bpkg_query_t bpkg_get_completed_chunks(bpkg_t* bpkg)
{

    mtree_t* mtree = bpkg->mtree;
    mtree_node_t** nodes = mtree->nodes;
    
    char** comp_chk_hashes = (char**)malloc(mtree->nchunks * sizeof(char*));
    int count = 0;
    for (int i = 0; i < mtree->nchunks; i++) {
        mtree_node_t* chk_node = mtree->chk_nodes[i];
        if (strcmp(chk_node->expected_hash, chk_node->computed_hash) == 0) {
            comp_chk_hashes[count++] = chk_node->expected_hash;
        }
    }

    bpkg_query_t qry;
    qry.hashes = comp_chk_hashes;
    qry.len = count;

    return qry;
}

/**
 * Gets the mininum of hashes to represented the current completion state
 * Example: If chunks representing start to mid have been completed but
 * 	mid to end have not been, then we will have (N_CHUNKS/2) + 1 hashes
 * 	outputted
 *
 * @param bpkg, constructed bpkg object
 * @return query_result, This structure will contain a list of hashes
 * 		and the number of hashes that have been retrieved
 */
bpkg_query_t bpkg_get_min_completed_hashes(bpkg_t* bpkg){

    mtree_node_t* subtree_root = bpkg_get_largest_completed_subtree(bpkg->mtree->root);
    
    bpkg_query_t qry;
    qry.hashes = bpkg_get_subtree_chunks(subtree_root);
    qry.len = mtree_get_nchunks_from_root(subtree_root, bpkg->mtree->height);
    return qry;
}

/**
 * Retrieves all chunk hashes given a certain an ancestor hash (or itself)
 * Example: If the root hash was given, all chunk hashes will be outputted
 * 	If the root's left child hash was given, all chunks corresponding to
 * 	the first half of the file will be outputted
 * 	If the root's right child hash was given, all chunks corresponding to
 * 	the second half of the file will be outputted
 * @param bpkg, constructed bpkg object
 * @return query_result, This structure will contain a list of hashes
 * 		and the number of hashes that have been retrieved
 */
bpkg_query_t bpkg_get_all_chunk_hashes_from_hash(bpkg_t* bpkg, char* query_hash, enum search_mode mode)
{

    mtree_node_t* node = bpkg_find_node_from_hash(bpkg->mtree, query_hash, EXPECTED);
    

    bpkg_query_t q_obj;

    q_obj.hashes = bpkg_get_subtree_chunks(node, bpkg->mtree->height);
    return q_obj;
}

/**
 * Deallocates the query result after it has been constructed from
 * the relevant queries above.
 */
void bpkg_query_destroy(bpkg_query_t* qobj)
{
    for (size_t i = 0; i < qobj->len; i++) {
    free(qobj->hashes[i]);
    }
    free(qobj->hashes);
    free(qobj);
}

/**
 * Deallocates memory at the end of the program,
 * make sure it has been completely deallocated
 */
void bpkg_destroy(bpkg_t* bobj)
{
    mtree_t* mtree = bobj->mtree;
    mtree_destroy(mtree);
    if (bobj) {
    mtree_destroy(bobj->mtree);
    free(bobj);
    }
}

int bpkg_check_chunk(mtree_node_t* node)
{
    if (strncmp(node->expected_hash, node->computed_hash, SHA256_HEXLEN) == 0) {
    debug_print("Chunk valid!");
    return 1;
    } else {
        debug_print("Chunk invalid:(");
        return 0;
    }
}
