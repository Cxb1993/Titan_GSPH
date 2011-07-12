/*
 * =====================================================================================
 *
 *       Filename:  dataread.cc
 *
 *    Description:  
 *
 *        Created:  03/15/2010 01:49:34 PM EDT
 *         Author:  Dinesh Kumar (dkumar), dkumar@buffalo.edu
 *        License:  GNU General Public License
 *
 * This software can be redistributed free of charge.  See COPYING
 * file in the top distribution directory for more details.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * =====================================================================================
 * $Id:$
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <iostream>
#include <string>
#include <limits>
using namespace std;

#ifdef HAVE_HDF5_H
#  include <hdf5.h>
#  include "hdf5calls.h"
#endif

#ifdef MULTI_PROC
# include <mpi.h>
#endif

#include <hashtab.h>
#include <bucket.h>
#include <buckstr.h>
#include <buckhead.h>
#include <particle.h>
#include <properties.h>

#include "sph_header.h"

int Read_Data(MatProps *matprops, TimeProps *timeprops, 
              PileProps *pileprops, FluxProps *fluxprops, 
              int *format)
{

  int i;
  double rotang, intfrict, bedfrict;
  const double deg2rad = 1.745329251994330e-02;

  ifstream in("simulation.data",ios::in);
  if (in.fail())
  {
    cerr << "ERROR: Can't find \"simulation.data\" input file." << endl;
    sph_exit(1);
  }
  int numpiles;
  // Pile properties
  in >> numpiles;
  pileprops->allocpiles(numpiles);

  // read data for all the piles
  for (i=0; i<numpiles; i++)
  {
    in >> pileprops->pileheight[i];
    in >> pileprops->xCen[i];
    in >> pileprops->yCen[i];
    in >> pileprops->majorrad[i];
    in >> pileprops->minorrad[i];
    in >> rotang;
    rotang *= deg2rad;
    pileprops->cosrot[i]=cos(rotang);
    pileprops->sinrot[i]=sin(rotang);
  }

  double time_scale = 1;
  // simulation time properties
  in >> timeprops->max_time;
  in >> timeprops->max_steps;
  in >> timeprops->timeoutput;
  in >> *(format);
  timeprops->TIME_SCALE = time_scale;
  timeprops->ndtimeoutput = timeprops->timeoutput/time_scale;
  timeprops->ndmax_time = timeprops->max_time/time_scale;

  // material properties
  in >> matprops->P_CONSTANT;
  in >> matprops->GAMMA;
  in >> matprops->particle_mass;
  in >> matprops->smoothing_length;
  in >> intfrict;
  in >> bedfrict;
  matprops->intfrict = deg2rad*intfrict;
  matprops->bedfrict = bedfrict;
  matprops->tanintfrict = tan(matprops->intfrict);
  matprops->sinintfrict = sin(matprops->intfrict);
  in.close();

    // Flux source data
  ifstream influx ("fluxsource.data",ios::in);
  if ( influx.good() )
  {
    fluxprops->have_src = true;
    influx >> fluxprops->xSrc;
    influx >> fluxprops->starttime;
    influx >> fluxprops->stoptime;
    influx >> fluxprops->tangvel;
    double vx = (fluxprops->tangvel*0.8746);
    timeprops->fluxrate_time = (matprops->smoothing_length)/abs(vx);
  }
  influx.close ();
  return 0;
}


int Read_Grid(HashTable **P_table, HashTable **BG_mesh, 
              vector<BucketHead> *partition_tab, MatProps *matprops, 
              PileProps *pileprops, FluxProps *fluxprops)
{
  int No_of_Buckets;
  int TABLE_SIZE=400000;
  double keyrange[2],mindom[DIMENSION],maxdom[DIMENSION];
  unsigned key[KEYLENGTH];
  Key tempkey;
  double hvars[6], min_crd[DIMENSION], max_crd[DIMENSION];
  char filename[14];
  int  Down[DIMENSION] = { 0, 1 };

  BucketStruct *bucket;
  Key neigh_keys[NEIGH_SIZE];
  int neigh_proc[NEIGH_SIZE];
  int bndtype;
  double pcoef[3];
  int i, j, k, myid;
  
  // infinity
  double infty;
  if ( numeric_limits<double>::has_infinity )
    infty = numeric_limits<double>::infinity();
  else
    infty = HUGE_VAL;
 
  // Read Hash-table related constants
  myid = 0;
#ifdef MULTI_PROC
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);
#endif
  sprintf(filename,"funky%04d.h5", myid);
  hid_t fp=GH5_fopen_serial (filename, 'r');
  
  // Read Hash table constants
  GH5_readdata(fp,"/hashtable_constants",hvars);
  keyrange[0]=hvars[0];
  keyrange[1]=hvars[1];
  mindom[0]  =hvars[2];  // min x
  maxdom[0]  =hvars[3];  // max x
  mindom[1]  =hvars[4];  // min y
  maxdom[1]  =hvars[5];  // max y
#ifdef THREE_D
  mindom[2]  =hvars[6];  // min z
  maxdom[2]  =hvars[7];  // max z
#endif

  // Create two new Hash-tables
  for(i=0;i<DIMENSION;i++) 
  {
    mindom[i] /= matprops->LENGTH_SCALE;
    maxdom[i] /= matprops->LENGTH_SCALE;
  } 
  *BG_mesh = new HashTable(keyrange,TABLE_SIZE, 2017, mindom, maxdom);

  // get the size of BG Mesh
  hsize_t dims[2];
  int num = GH5_getsize(fp,"/Buckets",dims);
  No_of_Buckets = (int) dims[0];

  // allocate memory for buckets
  bucket = new BucketStruct[No_of_Buckets];

  // read BG Mesh data
  GH5_read_grid_data (fp, "/Buckets", bucket);

  // clear out the partition table
  partition_tab->clear();
  for (i=0; i<No_of_Buckets; i++)
  {
    // hash-table keys
    for (j=0; j<KEYLENGTH; j++)
      key[j]=bucket[i].key[j];

    // coordinates
    min_crd[0]=bucket[i].xcoord[0];
    min_crd[1]=bucket[i].zcoord[0];
    max_crd[0]=bucket[i].xcoord[1];
    max_crd[1]=bucket[i].zcoord[1];

    if ( bucket[i].myproc != myid )
    {
      fprintf(stderr,"ERROR: Input data is not correct. Aborting.\n");
      fprintf(stderr,"myid = %d, data_proc = %d\n", myid, bucket[i].myproc);
#ifdef MULTI_PROC
      MPI_Abort(MPI_COMM_WORLD, myid);
#else 
      exit(1);
#endif   
    }

    for (j=0; j<NEIGH_SIZE; j++)
    {
      for (k=0; k<KEYLENGTH; k++)
        tempkey.key[k] = bucket[i].neighs[j*KEYLENGTH+k];
      neigh_keys[j] = tempkey;
      neigh_proc[j] = bucket[i].neigh_proc[j];
    }

    // Check if current bucket has flux source location
    if ( fluxprops->have_src )
    {
      double xsrc = fluxprops->xSrc;
      if ( (xsrc > min_crd[0]) && (xsrc < max_crd[0])
            && (bucket[i].buckettype == 2))
      {
        for (k=0; k<KEYLENGTH; k++)
          tempkey.key[k] = bucket[i].key[k];
        fluxprops->bucketsrckey = tempkey;
        // shift fluxsrc a little bit
        double dx = matprops->smoothing_length;
        int nn = (int) round((xsrc - min_crd[0])/dx);
        fluxprops->xSrc = min_crd[0] + nn*dx + dx/2.;
      }
    } 
    // Check if current bucket has center of any of the piles
    int numpiles = pileprops->NumPiles;
    for (j=0; j<numpiles; j++)
    {
      double *xcen = pileprops->xCen;
      if ((*(xcen+j) > min_crd[0])&&(*(xcen+j) < max_crd[0])&&
              (bucket[i].buckettype==2))
      {
        for (k=0; k<KEYLENGTH; k++)
          tempkey.key[k]=bucket[i].key[k];
        pileprops->CenBucket[j]=tempkey;
      }
    }
    
    // buckettype flag
    int btflag;
    switch( bucket[i].buckettype)
    {
      case 1:
        btflag = UNDERGROUND;
        break;
      case 2:
        btflag = MIXED;
        break;
      case 3:
        btflag = OVERGROUND;
        break;
      default:
        cerr << "ERROR: Unknown buckettype flag. Check the preoprocessor"<<endl;
        exit(1);
    }
        
    if ( bucket[i].boundary == 1)
    {
      bndtype = bucket[i].bndtype;
      for (j=0; j<3; j++)
        pcoef[j] = bucket[i].poly[j];
    }
    else
    {
      bndtype = -1;
      for (j=0; j<3; j++)
        pcoef[j] = 0;
    }
      
    Bucket *buck = new Bucket(key, min_crd, max_crd, btflag, bndtype, pcoef, 
                      myid, neigh_proc, neigh_keys);
    // just to be consistent everywhere
    if ( buck->which_neigh_proc(Down) == -1 )
    {
      BucketHead  bhead (buck->getKey(), 
                         0.5*(*buck->get_mincrd()+*buck->get_maxcrd()));
      partition_tab->push_back(bhead);
    }
    (*BG_mesh)->add(key,buck);
  }

  // Create hash-table for particles 
  *P_table = new HashTable(keyrange,TABLE_SIZE, 2017, mindom, maxdom); 
  return 0;
}
