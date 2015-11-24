/*
    Copyright (C) 2015 Tomas Flouri

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Contact: Tomas Flouri <Tomas.Flouri@h-its.org>,
    Heidelberg Institute for Theoretical Studies,
    Schloss-Wolfsbrunnenweg 35, D-69118 Heidelberg, Germany
*/

#include "delimit.h"

static double minbr;

static const unsigned int mask[256] =
 {
   0,  1,  1,  0,  1,  0,  0,  0,  1,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
 };

int pdist(char * a, char * b, int len)
{
  int i;
  int pdist = 0;

  for (i = 0; i < len; ++i)
  {
    if (mask[(int)a[i]] && mask[(int)b[i]] && (a[i] != b[i]))
      pdist++;
  }

  return pdist;
}

static int load_fasta(int tip_nodes_count, char ** headers, char ** seqdata)
{
  int i;

  /* open FASTA file */
  pll_fasta_t * fp = pll_fasta_open(opt_pdist_file, pll_map_fasta);
  if (!fp)
    fatal("Error opening file %s", opt_pdist_file);

  char * seq = NULL;
  char * hdr = NULL;
  long seqlen;
  long hdrlen;
  long seqno;

  /* read FASTA sequences and make sure they are all of the same length */
  int sites = -1;
  for (i = 0; pll_fasta_getnext(fp,&hdr,&hdrlen,&seq,&seqlen,&seqno); ++i)
  {
    if (i >= tip_nodes_count)
      fatal("FASTA file contains more sequences than expected");

    if (sites != -1 && sites != seqlen)
      fatal("FASTA file does not contain equal size sequences\n");

    if (sites == -1) sites = seqlen;

    headers[i] = hdr;
    seqdata[i] = seq;
  }

  /* did we stop reading the file because we reached EOF? */
  if (pll_errno != PLL_ERROR_FILE_EOF)
    fatal("Error while reading file %s", opt_pdist_file);

  /* close FASTA file */
  pll_fasta_close(fp);

  if (sites == -1)
    fatal("Unable to read alignment");

  if (i != tip_nodes_count)
    fatal("Some taxa are missing from FASTA file");

  return sites;
}

static int cb_ascending(const void * a, const void * b)
{
  if (*(double *)(a) < *(double *)(b))
    return -1;
  else if (*(double *)(a) > *(double *)(b))
    return 1;

  return 0;

}

static int cb_allnodes(rtree_t * node)
{
  return 1;
}

static int cb_short_trees(rtree_t * node)
{
  /* mark tip down but don't include them in the list */
  if (!node->left)
  {
    node->mark = 1;
    return 0;
  }

  if (node->left->mark && 
      node->right->mark &&
      node->left->length <= minbr &&
      node->right->length <= minbr)
  {
    node->mark = 1;
    if (node->parent)
    {
      /* if it's parent is the root of a short tree then dont include
         current node in the list, otherwise include it */
      if (node->parent->left->length <= minbr &&
          node->parent->right->length <= minbr)
      {
        return 0;
      }
      else
      {
        return 1;
      }
    }
    else  /* the current node is the root */
    {
      return 1;
    }
  }

  return 0;

}

static void hash_tips(rtree_t * root)
{
  int i;

  /*  obtain an array of pointers to tip names */
  rtree_t ** tipnodes = (rtree_t  **)xmalloc(root->leaves * sizeof(rtree_t *));
  rtree_query_tipnodes(root, tipnodes);

  /* create a libc hash table of size tip_count */
  hcreate(2*root->leaves);

  /* populate a libc hash table with tree tip labels */
  for (i = 0; i < root->leaves; ++i)
  {
    ENTRY entry;
    entry.key = tipnodes[i]->label;
    entry.data = (void *)(tipnodes[i]);
    hsearch(entry, ENTER);
  }
  free(tipnodes);
}


static void set_encode_sequence(rtree_t * node,
                                char * sequence,
                                int seqlen,
                                const unsigned int * map)
{
  unsigned int c;
  int i;

  /* iterate through sites and encode */
  for (i = 0; i < seqlen; ++i)
  {
    if ((c = map[(int)sequence[i]]) == 0)
      fatal("Illegal state code in tip \"%c\"", sequence[i]);

    sequence[i] = c;
  }

  /* set sequence to tip */
  node->sequence = sequence;

}

static void link_sequences(rtree_t * root, char ** headers, char ** sequence, int seqlen)
{
  int i;

  for (i = 0; i < root->leaves; ++i)
  {
    ENTRY query;
//    printf("Linking %s\n", headers[i]);
    query.key = headers[i];
    ENTRY * found = NULL;

    found = hsearch(query,FIND);

    if (!found)
      fatal("Sequence with header %s does not appear in the tree", headers[i]);
        
    set_encode_sequence((rtree_t *)(found->data), sequence[i], seqlen, pll_map_nt);
  }
}


static int all_pairwise_dist(rtree_t ** tip_node_list, int tip_list_count, int seqlen)
{
  int j,k;

  for (j = 0; j < tip_list_count; ++j)
    for (k = j+1; k < tip_list_count; ++k)
      if (pdist(tip_node_list[j]->sequence, tip_node_list[k]->sequence, seqlen))
        return 1;
  
  return 0;
}

void cmd_auto()
{
  FILE * out;
  rtree_t ** inner_node_list;
  rtree_t ** tip_node_list = NULL;
  int inner_list_count = 0;
  int tip_list_count = 0;
  int i,n;
  char ** seqdata = NULL;
  char ** headers = NULL;
  int seqlen = 0;

  /* attempt to open output file */
  out = opt_outfile ?
          xopen(opt_outfile,"w") : stdout;

  /* parse tree */
  if (!opt_quiet)
    fprintf(stdout, "Parsing tree file...\n");

  rtree_t * rtree = rtree_parse_newick(opt_treefile);
  if (!rtree)
    fatal("Error: --minbr_auto is only implemented for rooted trees");

  /* for p-distance computation load an alignment from a FASTA file and map
     the sequences to the tree tips */

  if (!opt_quiet)
    fprintf(stdout, "Parsing FASTA file %s...\n", opt_pdist_file);
  
  /* allocate arrays to store FASTA headers and sequences */
  headers = (char **)calloc(rtree->leaves, sizeof(char *));
  seqdata = (char **)calloc(rtree->leaves, sizeof(char *));

  seqlen = load_fasta(rtree->leaves, headers, seqdata);

  hash_tips(rtree);

  /* find sequences in hash table and link them with the corresponding taxa */
  link_sequences(rtree, headers, seqdata, seqlen);

  /* destroy hash table */
  hdestroy();
    


  /* get inner nodes that are roots of of the largest short subtrees. Short are
     such subtrees where all branch lengths within them are less or equal to
     opt_subtree_short. The largest such subtrees are those that are not
     subtrees of short subtrees.
  */
  inner_node_list = (rtree_t **)xmalloc((rtree->leaves-1)*sizeof(rtree_t *));


  double * branch_lengths = (double *)xmalloc((2*rtree->leaves-1)*sizeof(double));
  rtree_t ** allnodes_list = (rtree_t **)xmalloc((2*rtree->leaves-1)*sizeof(rtree_t *));
  int allnodes_count;

  /* get list of all nodes, extract branch lengths and sort them in ascending 
     order */
  allnodes_count = rtree_traverse_postorder(rtree, cb_allnodes, allnodes_list);
  assert(allnodes_count == 2*rtree->leaves-1);
  for (i = 0; i < allnodes_count; ++i)
    branch_lengths[i] = allnodes_list[i]->length;
  qsort(branch_lengths, allnodes_count, sizeof(double), cb_ascending);
  free(allnodes_list);


  printf("Computing all pairwise p-distances ...\n");

  tip_node_list = (rtree_t **)xmalloc((rtree->leaves)*sizeof(rtree_t *));


  int minfound = 0;
  /* go through all branch lengths */
  for (n = 1; n < allnodes_count && !minfound; ++n)
  {
    minbr = branch_lengths[n];
    inner_list_count = rtree_traverse_postorder(rtree,
                                                cb_short_trees,
                                                inner_node_list);

    for (i = 0; i < inner_list_count && !minfound; ++i)
    {
      /* traverse the roots and grab the tips */
      tip_list_count = rtree_query_tipnodes(inner_node_list[i], tip_node_list);
      minfound = all_pairwise_dist(tip_node_list, tip_list_count, seqlen);
      if (minfound) break;
    }
  }

  if (minfound && n != 1)
    printf("Minimum branch length (--minbr) should be set to %.10f\n", branch_lengths[n-1]);
  else
    printf("Minimum branch length (--minbr) should be set to 0\n");


  free(branch_lengths);
  free(inner_node_list);
  free(tip_node_list);

  for (i = 0; i < rtree->leaves; ++i)
  {
    free(seqdata[i]);
    free(headers[i]);
  }
  free(seqdata);
  free(headers);

  /* deallocate tree structure */
  rtree_destroy(rtree);

  if (!opt_quiet)
    fprintf(stdout, "Done...\n");
  
  if (opt_outfile)
    fclose(out);
}
