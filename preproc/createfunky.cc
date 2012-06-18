/*
 * =====================================================================================
 *
 *       Filename:  createfunky.cc
 *
 *    Description:  
 *
 *        Created:  03/27/2010 05:41:33 PM EDT
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
# include <config.h>
#endif

#ifdef HAVE_HDF5_H
# include <hdf5.h>
# include <hdf5calls.h>
#endif

#include <iomanip>
#include <sstream>
#include <string>
using namespace std;

#include "buckstr.h"
#include "preprocess.h"

string create_filename (string base, string ext, const int padding, int myid)
{
  ostringstream ss;
  ss << base << setw(padding) << setfill('0') << myid << ext; 
  return ss.str();
}

void createfunky(int myid, int nhtvars, double * htvars, vector <BucketStruct> & bg)
{

  int i, j;
  string basename = "funky";
  string exten    = ".h5";
  const int padding = 4;

  // create filenames
  string fname = create_filename (basename, exten, padding, myid);
  hid_t fp = GH5_fopen_serial (fname.c_str(),'w');

  // write HASH TABLE constants
  int dims1[2]={nhtvars,0};
  GH5_WriteS (fp ,"/hashtable_constants", dims1, (void *) htvars, 0, 0, DOUBLETYPE);

  // copy vector to an regular array
  int nbuck = (int) bg.size();
  BucketStruct *myBucks = new BucketStruct [nbuck];
  copy(bg.begin(), bg.end(), myBucks);

    // write Background Grid data
  GH5_write_grid_data(fp,"/Buckets", nbuck, myBucks);

  delete [] myBucks;
    // close file
  GH5_fclose(fp);

  return;
}
