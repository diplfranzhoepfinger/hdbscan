/*
 * cluster.c
 *
 * Copyright 2018 Onalenna Junior Makhura
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file cluster.c
 * 
 * @author Onalenna Junior Makhura (ojmakhura@roguesystems.co.bw)
 * 
 * @brief Implementation of the functions from cluster.h
 * 
 * @version 3.1.6
 * @date 2018-01-10
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#include "hdbscan/cluster.h"

#include <float.h>

cluster* cluster_init(cluster* cl, int32_t label, cluster* parent, double birthLevel, int32_t numPoints){
	if(cl == NULL){
		cl = (cluster*)malloc(sizeof(cluster));
	}
	if(cl == NULL){
		printf("ERROR: cluster_init - Could not allocate memory for cluster");
	} else {

		cl->label = label;
		cl->birthLevel = birthLevel;
		cl->deathLevel = 0;
		cl->numPoints = numPoints;
		cl->offset = 0;
		cl->stability = 0;
		cl->propagatedStability = 0;
		cl->propagatedLowestChildDeathLevel = DBL_MAX;
		cl->numConstraintsSatisfied = 0;
		cl->propagatedNumConstraintsSatisfied = 0;
		cl->parent = parent;
		if (cl->parent != NULL)
			cl->parent->hasChildren = TRUE;
		cl->hasChildren = FALSE;
		cl->virtualChildCluster = gl_oset_nx_create_empty (GL_ARRAY_OSET, (gl_setelement_compar_fn) int_compare, NULL);
		cl->propagatedDescendants = ptr_array_list_init(1, cluster_compare);
	}

	return cl;
}

void cluster_destroy(cluster* cl){
	if(cl != NULL){
		if(cl->virtualChildCluster != NULL){
			gl_oset_free(cl->virtualChildCluster);
			cl->virtualChildCluster = NULL;
		}

		if(cl->propagatedDescendants != NULL){
			array_list_delete(cl->propagatedDescendants);
			cl->propagatedDescendants = NULL;
		}
		free(cl);
	}
}


int cluster_detach_points(cluster* cl, int numPoints, double level){

	cl->numPoints -= numPoints;
	cl->stability += (numPoints * (1 / level - 1 / cl->birthLevel));

	if (cl->numPoints == 0)
		cl->deathLevel = level;
	else if (cl->numPoints < 0){
		printf("ERROR: Cluster %d has %d points.\n", cl->label, cl->numPoints);
		return CLUSTER_ERROR;
	}

	return CLUSTER_SUCCESS;
}

void cluster_propagate(cluster* cl){

	if (cl->parent != NULL) {
		//Propagate lowest death level of any descendants:
        if (cl->propagatedLowestChildDeathLevel == DBL_MAX){
        	cl->propagatedLowestChildDeathLevel = cl->deathLevel;
		}

		if (cl->propagatedLowestChildDeathLevel < cl->parent->propagatedLowestChildDeathLevel){
			cl->parent->propagatedLowestChildDeathLevel = cl->propagatedLowestChildDeathLevel;
		}

		if (cl->hasChildren == FALSE || cl->numConstraintsSatisfied > cl->propagatedNumConstraintsSatisfied) {
			cl->parent->propagatedNumConstraintsSatisfied += cl->numConstraintsSatisfied;
			cl->parent->propagatedStability += cl->stability;
			array_list_append(cl->parent->propagatedDescendants, &cl);
		}
		else if (cl->numConstraintsSatisfied < cl->propagatedNumConstraintsSatisfied) {

			cl->parent->propagatedNumConstraintsSatisfied += cl->propagatedNumConstraintsSatisfied;
			cl->parent->propagatedStability += cl->propagatedStability;

			for(int32_t i = 0; i < cl->propagatedDescendants->size; i++)
			{
				cluster* c = NULL;
				array_list_value_at(cl->propagatedDescendants, i, &c);
				cluster* tmp = c;
				array_list_append(cl->parent->propagatedDescendants, &tmp);
			}
		}
		else if (cl->numConstraintsSatisfied == cl->propagatedNumConstraintsSatisfied) {
			//Chose the parent over descendants if there is a tie in stability:
			if (cl->stability >= cl->propagatedStability) {
				cl->parent->propagatedNumConstraintsSatisfied += cl->numConstraintsSatisfied;
				cl->parent->propagatedStability += cl->stability;
				array_list_append(cl->parent->propagatedDescendants, &cl);
			}

			else {
				cl->parent->propagatedNumConstraintsSatisfied += cl->propagatedNumConstraintsSatisfied;
				cl->parent->propagatedStability += cl->propagatedStability;

				for(int32_t i = 0; i < cl->propagatedDescendants->size; i++)
				{
					cluster *c = NULL;
					array_list_value_at(cl->propagatedDescendants, i, &c);
					cluster *tmp = c;
					array_list_append(cl->parent->propagatedDescendants, &tmp);
				}
			}
		}
	}
}


int cluster_add_points_to_virtual_child_cluster(cluster* cl, gl_oset_t points){
	
	for(size_t i = 0; i < points->count; i++){
		int32_t d;
		gl_oset_value_at(points, i, &d);
		gl_oset_nx_add(cl->virtualChildCluster, d);
	}

	return 1;
}

boolean cluster_virtual_child_contains_point(cluster* cl, int32_t point){
	return gl_oset_search(cl->virtualChildCluster, point);
}

void cluster_add_virtual_child_constraints_satisfied(cluster* cl, int32_t numConstraints){

	cl->propagatedNumConstraintsSatisfied += numConstraints;
}

void cluster_add_constraints_satisfied(cluster* cl, int32_t numConstraints){

	cl->numConstraintsSatisfied += numConstraints;
}

void cluster_release_virtual_child(cluster* cl){

	if(cl->parent != NULL){
		cluster_destroy(cl->parent);
	}
}

int32_t cluster_compare(const void* a, const void* b)
{
	const cluster* c1 = (cluster *)a;
	const cluster* c2 = (cluster *)b;
	
	if (c1 > c2) {
		return 1;
	}
	if (c1 == c2) {
		return 0;
	}
	
	return -1;
}