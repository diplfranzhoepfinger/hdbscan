/*
 * hdbscan.c
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
 * @file hdbscan.c
 * @author Onalenna Junior Makhura (ojmakhura@roguesystems.co.bw)
 * 
 * @brief Core implementation of the HDBSCAN algorithm
 * 
 * @version 
 * @date 2018-01-10
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#include "hdbscan/hdbscan.h"
#include <assert.h>
#include <time.h>
#include <math.h>
#include "listlib/set.h"

/**
 * @brief Calculation of the size of the dataset based on whether it is
 * rowwise or not. If it rowwise, then each row is one value as a vector,
 * otherwise each element dataset[i, j] is an idividual element .
 * 
 * @param rows 
 * @param cols 
 * @param rowwise 
 * @return uint 
 */
uint hdbscan_get_dataset_size(uint rows, uint cols, boolean rowwise){
    if(rowwise == 1){
        return rows;
    } else{
        return rows * cols;
    }
}

/**
 * @brief Removes the set of points from their parent Cluster, and creates a new Cluster, provided the
 * clusterId is not 0 (noise).
 * 
 * @param points The set of points to be in the new Cluster
 * @param clusterLabels An array of cluster labels, which will be modified
 * @param parentCluster The parent Cluster of the new Cluster being created
 * @param clusterLabel The label of the new Cluster
 * @param edgeWeight The edge weight at which to remove the points from their previous Cluster
 * @return cluster*
 */
//cluster* hdbscan_create_new_cluster(hdbscan* sc, gl_oset_t points, int* clusterLabels, cluster* parentCluster, int clusterLabel, double edgeWeight){
cluster* hdbscan_create_new_cluster(hdbscan* sc, set_t* points, int* clusterLabels, cluster* parentCluster, int clusterLabel, double edgeWeight){
#pragma omp parallel for
	//for(int i = 0; i < points->count; i++){
	for(int i = 0; i < points->size; i++){
		int d ;
		//gl_oset_value_at(points, i, &d);
		set_value_at(points, i, &d);
		clusterLabels[d] = clusterLabel;
	}

	//cluster_detach_points(parentCluster, points->count, edgeWeight);
	cluster_detach_points(parentCluster, points->size, edgeWeight);
	if (clusterLabel != 0) {
		//cluster* new = cluster_init(NULL, clusterLabel, parentCluster, edgeWeight, points->count);
		cluster* new = cluster_init(NULL, clusterLabel, parentCluster, edgeWeight, points->size);
		return new;
	} else{
		cluster_add_points_to_virtual_child_cluster(parentCluster, points);
		return NULL;
	}

}

hdbscan* hdbscan_init(hdbscan* sc, uint minPoints){
	if(sc == NULL)
		sc = (hdbscan*) malloc(sizeof(hdbscan));

	if(sc == NULL){
		printf("Error: Could not allocate memory for HDBSCAN.\n");
	} else{
		sc->minPoints = minPoints;
		sc->selfEdges = TRUE;
		sc->hierarchy = NULL; //hashtable_init(43, H_LONG, sizeof(long), sizeof(hierarchy_entry *), long_compare);
		sc->clusterStabilities = NULL; //hashtable_init(43, H_INT, sizeof(int32_t), sizeof(void *), int_compare);

		sc->dataSet = NULL;
		sc->constraints = NULL;
		sc->clusterLabels = NULL;
		sc->clusters = NULL;
		sc->coreDistances = NULL;
		sc->outlierScores = NULL;

	}
	return sc;
}

void hdbscan_minimal_clean(hdbscan* sc){

	if(sc->clusterLabels != NULL){
		free(sc->clusterLabels);
		sc->clusterLabels = NULL;
	}

	if(sc->outlierScores != NULL){
		free(sc->outlierScores);
		sc->outlierScores = NULL;
	}

	if(sc->mst != NULL){
		graph_destroy(sc->mst);
		sc->mst = NULL;
	}

	if(sc->constraints != NULL){

		for(int32_t i = 0; i < sc->constraints->size; i++)
		{
			constraint* c;
			array_list_value_at(sc->constraints, i, &c);
			constraint_destroy(c);
		}

		array_list_delete(sc->constraints);
		sc->constraints = NULL;
	}

	if(sc->clusterStabilities != NULL){

		for(size_t i = 0; i < set_size(sc->clusterStabilities->keys); i++){

			int32_t key, ret = 1;
			set_value_at(sc->clusterStabilities->keys, i, &key);
			//ret = hashtable_remove(sc->clusterStabilities, &key);

			if(!ret)
			{
				printf("Could not remove key %d from the table\n", key);
			}
		}

		hashtable_destroy(sc->clusterStabilities, NULL, NULL);
		sc->clusterStabilities = NULL;
	}

	if(hashtable_size(sc->hierarchy) > 0){
		hashtable_clear(sc->hierarchy, NULL, hdbscan_destroy_hierarchical_entry);
	}

	if(sc->clusters != NULL){

		for(int32_t i = 0; i < sc->clusters->size; i++)
		{
			cluster* cl;
			array_list_value_at(sc->clusters, i, &cl);
			cluster_destroy(cl);
		}

		array_list_delete(sc->clusters);
	}
	sc->clusters = NULL;
}

void hdbscan_clean(hdbscan* sc){

	distance_clean(&sc->distanceFunction);
	hdbscan_minimal_clean(sc);
}

void hdbscan_destroy(hdbscan* sc){
	hdbscan_clean(sc);

	if(sc->hierarchy != NULL){
		hashtable_destroy(sc->hierarchy, NULL, hdbscan_destroy_hierarchical_entry);
		sc->hierarchy = NULL;
	}

	if(sc != NULL){
		free(sc);
	}
}

/**
 *
 */
int hdbscan_do_run(hdbscan* sc){

	//sc->clusters = g_ptr_array_sized_new(csize);
	int err = hdbscan_construct_mst(sc);

	if(err == HDBSCAN_ERROR){
		printf("Error: Could not construct the minimum spanning tree.\n");
		return HDBSCAN_ERROR;
	}

	//printf("graph_quicksort_by_edge_weight\n");
	graph_quicksort_by_edge_weight(sc->mst);

	double pointNoiseLevels[sc->numPoints];
	int pointLastClusters[sc->numPoints];

	//printf("hdbscan_compute_hierarchy_and_cluster_tree\n");
	hdbscan_compute_hierarchy_and_cluster_tree(sc, 0, pointNoiseLevels, pointLastClusters);
	//printf("hdbscan_propagate_tree\n");
	int infiniteStability = hdbscan_propagate_tree(sc);

	//printf("hdbscan_find_prominent_clusters\n");
	hdbscan_find_prominent_clusters(sc, infiniteStability);
	return HDBSCAN_SUCCESS;
}

int hdbscan_rerun(hdbscan* sc, int32_t minPts){
	// clean the hdbscan
	hdbscan_minimal_clean(sc);

	sc->selfEdges = TRUE;
	sc->hierarchy = NULL;
	sc->clusterStabilities = NULL;
	sc->dataSet = NULL;
	sc->constraints = NULL;
	sc->clusterLabels = NULL;
	sc->clusters = NULL;
	sc->coreDistances = NULL;
	sc->outlierScores = NULL;
	sc->minPoints = minPts;
	sc->distanceFunction.numNeighbors = minPts-1;
	distance_get_core_distances(&(sc->distanceFunction));

	return hdbscan_do_run(sc);
}

int hdbscan_run(hdbscan* sc, void* dataset, uint rows, uint cols, boolean rowwise, uint datatype){

	if(sc == NULL){
		printf("hdbscan_run: sc has not been initialised.\n");
		return HDBSCAN_ERROR;
	}
	//printf("distance_init\n");
	distance_init(&sc->distanceFunction, _EUCLIDEAN, datatype);

	//printf("hdbscan_get_dataset_size\n");
	sc->numPoints = hdbscan_get_dataset_size(rows, cols, rowwise);
	distance_compute(&(sc->distanceFunction), dataset, rows, cols, sc->minPoints-1);

	int32_t csize = sc->numPoints/5;
	sc->clusters = ptr_array_list_init(csize, cluster_compare);
	sc->hierarchy = hashtable_init(csize, H_DOUBLE, H_PTR, long_compare);
	sc->clusterStabilities = hashtable_init(csize, H_INT, H_PTR, int_compare);

	return hdbscan_do_run(sc);
}

/**
 * Calculates the number of constraints satisfied by the new clusters and virtual children of the
 * parents of the new clusters.
 * @param newClusterLabels Labels of new clusters
 * @param clusters An vector of clusters
 * @param constraints An vector of constraints
 * @param clusterLabels an array of current cluster labels for points
 */
void hdbscan_calculate_num_constraints_satisfied(hdbscan* sc, gl_oset_t* newClusterLabels,	int* currentClusterLabels){

	if(array_list_size(sc->constraints) == 0)
	{
		return;
	}
}

/**
 * @brief 
 * 
 * @param sc 
 * @param compactHierarchy 
 * @param pointNoiseLevels 
 * @param pointLastClusters 
 * @return int 
 */

int hdbscan_compute_hierarchy_and_cluster_tree(hdbscan* sc, int compactHierarchy, double* pointNoiseLevels, int* pointLastClusters){

	int64_t lineCount = 0; // Indicates the number of lines written into
								// hierarchyFile.

	//The current edge being removed from the MST:
	int currentEdgeIndex = sc->mst->edgeWeights->size - 1;

	int nextClusterLabel = 2;
	boolean nextLevelSignificant = TRUE;
	//The previous and current cluster numbers of each point in the data set:
	int32_t numVertices = sc->mst->numVertices;
	int32_t previousClusterLabels[numVertices];
	int32_t currentClusterLabels[numVertices];
#pragma omp parallel for
	for(int32_t i = 0; i < numVertices; i++){
		previousClusterLabels[i] = 1;
		currentClusterLabels[i] = 1;
	}

	//A list of clusters in the cluster tree, with the 0th cluster (noise) null:
	cluster* c = NULL;
	array_list_append(sc->clusters, &c);

	c = cluster_init(NULL, 1, NULL, NAN, numVertices);
	array_list_append(sc->clusters, &c);

	//Sets for the clusters and vertices that are affected by the edge(s) being removed:
	set_t* affectedClusterLabels = set_init(sizeof(int32_t), int_compare);
	set_t* affectedVertices = set_init(sizeof(int32_t), int_compare);

//#pragma omp parallel
	while (currentEdgeIndex >= 0) {
		double currentEdgeWeight;
		double_array_list_data(sc->mst->edgeWeights, currentEdgeIndex, &currentEdgeWeight);
		//int cc = 0;
		ArrayList* newClusters = array_list_init(2, sizeof(cluster *), cluster_compare);
		//Remove all edges tied with the current edge weight, and store relevant clusters and vertices:
		double tmp_w;
		double_array_list_data(sc->mst->edgeWeights, currentEdgeIndex, &tmp_w);
		while(currentEdgeIndex >= 0 && tmp_w == currentEdgeWeight){

			int firstVertex;
			int_array_list_data(sc->mst->verticesA, currentEdgeIndex, &firstVertex);
			int secondVertex;
			int_array_list_data(sc->mst->verticesB, currentEdgeIndex, &secondVertex);
			graph_remove_edge(sc->mst, firstVertex, secondVertex);

			if (currentClusterLabels[firstVertex] == 0) {
				currentEdgeIndex--;
				if(currentEdgeIndex >= 0)
					double_array_list_data(sc->mst->edgeWeights, currentEdgeIndex, &tmp_w);
				continue;
			}

			set_insert(affectedVertices, &firstVertex);
			set_insert(affectedVertices, &secondVertex);
			set_insert(affectedClusterLabels, currentClusterLabels + firstVertex);

			currentEdgeIndex--;
			if(currentEdgeIndex >= 0)
			{
				double_array_list_data(sc->mst->edgeWeights, currentEdgeIndex, &tmp_w);
			}
		}

		if(affectedClusterLabels->size < 1){
			continue;
		}

		//Check each cluster affected for a possible split:
		while(affectedClusterLabels->size > 0){

			int examinedClusterLabel;
			set_remove_at(affectedClusterLabels, affectedClusterLabels->size-1, &examinedClusterLabel);
			set_t* examinedVertices = set_init(sizeof(int32_t), int_compare);

			//Get all affected vertices that are members of the cluster currently being examined:
			size_t it = 0;
			while(it < affectedVertices->size){
				int n;
				set_value_at(affectedVertices, it, &n);
				if (currentClusterLabels[n] == examinedClusterLabel) {
					set_insert(examinedVertices, &n);
					set_remove(affectedVertices, &n);
				} else{
					it++;
				}
			}

			set_t* firstChildCluster = set_init (sizeof(int32_t), int_compare);
			set_t* unexploredFirstChildClusterPoints = set_init (sizeof(int32_t), int_compare);

			int32_t numChildClusters = 0;

			/* Check if the cluster has split or shrunk by exploring the graph from each affected
			 * vertex.  If there are two or more valid child clusters (each has >= minClusterSize
			 * points), the cluster has split.
			 * Note that firstChildCluster will only be fully explored if there is a cluster
			 * split, otherwise, only spurious components are fully explored, in order to label
			 * them noise.
			 */
			while (examinedVertices->size > 0) {
				//gl_oset_t constructingSubCluster = gl_oset_nx_create_empty (GL_ARRAY_OSET, (gl_setelement_compar_fn) int_compare, NULL);
				set_t* constructingSubCluster = set_init (sizeof(int32_t), int_compare);
				IntArrayList* unexploredSubClusterPoints = int_array_list_init();

				boolean anyEdges = FALSE;
				boolean incrementedChildCount = FALSE;

				int32_t rootVertex;
				set_remove_at(examinedVertices, examinedVertices->size-1, &rootVertex);
				set_insert(constructingSubCluster, &rootVertex);
				int_array_list_append(unexploredSubClusterPoints, rootVertex);

				//Explore this potential child cluster as long as there are unexplored points:
				while (unexploredSubClusterPoints->size > 0) {
					int vertexToExplore;
					int_array_list_data(unexploredSubClusterPoints, (unexploredSubClusterPoints->size)-1, &vertexToExplore);
					int_array_list_pop(unexploredSubClusterPoints);

					IntArrayList* v = sc->mst->edges[vertexToExplore];

					for(int i = 0; i < v->size; i++){
						int neighbor;
						int_array_list_data(v, i, &neighbor);
						anyEdges = TRUE;
						bool p = set_insert(constructingSubCluster, &neighbor);

						if(p){
							int_array_list_append(unexploredSubClusterPoints, neighbor);
							set_remove(examinedVertices, &neighbor);
						}
					}

					//Check if this potential child cluster is a valid cluster:
					if(incrementedChildCount == FALSE && constructingSubCluster->size >= sc->minPoints && anyEdges == TRUE){
						incrementedChildCount = TRUE;
						numChildClusters++;

						//If this is the first valid child cluster, stop exploring it:
						if (firstChildCluster->size == 0) {
							int d;
							for(int i = 0; i < constructingSubCluster->size; i++){
								set_value_at(constructingSubCluster, i, &d);
								set_insert(firstChildCluster, &d);
							}

							for(int i = 0; i < unexploredSubClusterPoints->size; i++){
								int_array_list_data(unexploredSubClusterPoints, i, &d);
								set_insert(unexploredFirstChildClusterPoints, &d);
							}
							break;
						}
					}
				}

				//If there could be a split, and this child cluster is valid:
				if(numChildClusters >= 2 && constructingSubCluster->size >= sc->minPoints && anyEdges == TRUE){
					//Check this child cluster is not equal to the unexplored first child cluster:
					int32_t firstChildClusterMember;
					set_value_at(firstChildCluster, firstChildCluster->size-1, &firstChildClusterMember);
					//set_fin
					if(set_find(constructingSubCluster, &firstChildClusterMember) > -1){
						numChildClusters--;
					}

					//Otherwise, create a new cluster:
					else {
						cluster* examinedCluster = NULL;
						array_list_value_at(sc->clusters, examinedClusterLabel, &examinedCluster);
						cluster* newCluster = hdbscan_create_new_cluster(sc, constructingSubCluster, currentClusterLabels, examinedCluster, nextClusterLabel, currentEdgeWeight);
						array_list_append(newClusters, &newCluster);
						nextClusterLabel++;
						array_list_append(sc->clusters, &newCluster);
					}
				}

				//If this child cluster is not valid cluster, assign it to noise:
				else if(constructingSubCluster->size < sc->minPoints || anyEdges == FALSE){

					cluster* examinedCluster = NULL;
					array_list_value_at(sc->clusters, examinedClusterLabel, &examinedCluster);
					cluster* newCluster = hdbscan_create_new_cluster(sc, constructingSubCluster, currentClusterLabels, examinedCluster, 0, currentEdgeWeight);

					for (int i = 0; i < constructingSubCluster->size; i++) {
						int point;
						set_value_at(constructingSubCluster, i, &point);
						pointNoiseLevels[point] = currentEdgeWeight;
						pointLastClusters[point] = examinedClusterLabel;
					}

					cluster_destroy(newCluster);

				}
				/********************
				 * Clean up constructing subcluster
				 *******************/
				set_delete(constructingSubCluster);
				int_array_list_delete(unexploredSubClusterPoints);
			}

			//Finish exploring and cluster the first child cluster if there was a split and it was not already clustered:
			int dd;
			set_value_at(firstChildCluster, 0, &dd);
			if (numChildClusters >= 2 && currentClusterLabels[dd] == examinedClusterLabel) {
				while(unexploredFirstChildClusterPoints->size > 0){
					int vertexToExplore;
					set_remove_at(unexploredFirstChildClusterPoints, unexploredFirstChildClusterPoints->size-1, &vertexToExplore);
					IntArrayList* v = (sc->mst->edges)[vertexToExplore];
					int neighbor;
					for (int i = 0; i < v->size; i++) {
						int_array_list_data(v, i, &neighbor);
						if (set_insert(firstChildCluster, &neighbor)) {
							set_insert(unexploredFirstChildClusterPoints, &neighbor);
						}
					}
				}

				cluster* examinedCluster = NULL;
				array_list_value_at(sc->clusters, examinedClusterLabel, &examinedCluster);
				cluster* newCluster = hdbscan_create_new_cluster(sc, firstChildCluster, currentClusterLabels, examinedCluster, nextClusterLabel, currentEdgeWeight);
				array_list_append(newClusters, &newCluster);
				nextClusterLabel++;
				array_list_append(sc->clusters, &newCluster);
			}
			set_delete(firstChildCluster);
			set_delete(unexploredFirstChildClusterPoints);
			set_delete(examinedVertices);
		}

		if (compactHierarchy == FALSE || nextLevelSignificant == TRUE || array_list_size(newClusters) > 0) {
			lineCount++;
			hierarchy_entry* entry = hdbscan_create_hierarchy_entry();
			entry->edgeWeight = currentEdgeWeight;
			entry->labels = (int32_t*)malloc(numVertices * sizeof(int32_t));
			memcpy(entry->labels, previousClusterLabels, numVertices * sizeof(int32_t));

			hashtable_insert(sc->hierarchy, &lineCount, &entry);
		}

		// Assign offsets and calculate the number of constraints satisfied:
		set_t* newClusterLabels = set_init(sizeof(int32_t), int_compare);

		for(size_t i = 0; i < array_list_size(newClusters); i++)
		{
			cluster* newCluster = NULL;
			array_list_value_at(newClusters, i, &newCluster);
			newCluster->offset = lineCount;
			set_insert(newClusterLabels, &lineCount);
		}

		if (set_size(newClusterLabels) > 0){
			//calculateNumConstraintsSatisfied(newClusterLabels, currentClusterLabels);
		}

		for (uint i = 0; i < numVertices; i++) {
			previousClusterLabels[i] = currentClusterLabels[i];
		}

		if (array_list_size(newClusters) == 0){
			nextLevelSignificant = FALSE;
		} else{
			nextLevelSignificant = TRUE;
		}

		array_list_delete(newClusters);
		set_delete(newClusterLabels);
	}

	hierarchy_entry* entry = hdbscan_create_hierarchy_entry();
	entry->edgeWeight = 0.0;
	entry->labels = (int32_t*) malloc(numVertices * sizeof(int32_t));
	memset(entry->labels, 0, numVertices * sizeof(int32_t));
	// Write out the final level of the hierarchy (all points noise):

	int64_t l = 0;
	hashtable_insert(sc->hierarchy, &l, &entry);
	lineCount++;

	set_delete(affectedClusterLabels);
	set_delete(affectedVertices);

	return HDBSCAN_SUCCESS;
}

hierarchy_entry* hdbscan_create_hierarchy_entry(){
	hierarchy_entry* entry = (hierarchy_entry *) malloc(sizeof(hierarchy_entry));
	entry->labels = NULL;
	entry->edgeWeight = 0.0;
	return entry;
}

void print_distances(hdbscan* sc){
	for (int i = 0; i < sc->numPoints; i++) {
		printf("[");
		for (int j = 0; j < sc->numPoints; j++) {
			printf("%f ", distance_get(&sc->distanceFunction, i, j));
		}

		printf("]\n");
	}
	printf("\n");
}

void print_graph_components(IntArrayList *nearestMRDNeighbors, IntArrayList *otherVertexIndices, DoubleArrayList *nearestMRDDistances){
	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>nearestMRDNeighbors>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	for(int i = 0; i < nearestMRDNeighbors->size; i++){
		int d;
		int_array_list_data(nearestMRDNeighbors, i, &d);
		printf("%d ", d);
	}

	printf("\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>otherVertexIndices>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	for(int i = 0; i < otherVertexIndices->size; i++){
		int d;
		int_array_list_data(otherVertexIndices, i, &d);
		printf("%d ", d);
	}

	printf("\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>nearestMRDDistances>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	//for(int i = 0; i < nearestMRDDistances->size; i++){
		//printf("%f \n", *(double_array_list_data(nearestMRDDistances, i)));
	//}

	printf("\n>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n\n\n\n");
}

int hdbscan_construct_mst(hdbscan* sc){
	//printf("hdbscan_construct_mst begin\n");
	double*  coreDistances = sc->distanceFunction.coreDistances;

	int selfEdgeCapacity = 0;
	const uint size = sc->numPoints;
	if (sc->selfEdges == TRUE){
		selfEdgeCapacity = size;
	}

	//One bit is set (true) for each attached point, or unset (false) for unattached points:
	boolean attachedPoints[size];
#pragma omp parallel for
	for(int i = 0; i < size-1; i++){
		attachedPoints[i] = FALSE;
	}

	//printf("1 -- hdbscan_construct_mst\n");
	//The MST is expanded starting with the last point in the data set:
	int currentPoint = size - 1;
	attachedPoints[size - 1] = TRUE;

	//Each point has a current neighbor point in the tree, and a current nearest distance:
	int ssize = size - 1 + selfEdgeCapacity;
	IntArrayList* nearestMRDNeighbors = int_array_list_init_full(ssize, 0);
	//printf("2 -- hdbscan_construct_mst\n");
	if(nearestMRDNeighbors == NULL){
		printf("ERROR: hdbscan_construct_mst - Could not construct nearestMRDNeighbors");
		return HDBSCAN_ERROR;
	}

	//printf("2 -- hdbscan_construct_mst\n");
	//Create an array for vertices in the tree that each point attached to:
	IntArrayList* otherVertexIndices = int_array_list_init_full(ssize, 0);

	if(otherVertexIndices == NULL){
		printf("ERROR: hdbscan_construct_mst - Could not construct otherVertexIndices");
		return HDBSCAN_ERROR;
	}

	DoubleArrayList* nearestMRDDistances = double_array_list_init_full(ssize, DBL_MAX);

	if(nearestMRDDistances == NULL){
		printf("ERROR: hdbscan_construct_mst - Could not construct nearestMRDDistances");
		return HDBSCAN_ERROR;
	}

//#pragma omp parallel for
	//Continue attaching points to the MST until all points are attached:
	for (uint numAttachedPoints = 1; numAttachedPoints < size; numAttachedPoints++) {
		int nearestMRDPoint = -1;
		double nearestMRDDistance = DBL_MAX;

		//Iterate through all unattached points, updating distances using the current point:
#pragma omp parallel for
		for (unsigned int neighbor = 0; neighbor < size; neighbor++) {

			if (currentPoint == neighbor) {
				continue;
			}

			if (attachedPoints[neighbor] == TRUE) {
				continue;
			}
			//printf("5 -- hdbscan_construct_mst neighbor = %d and currentPoint = %d\n", neighbor, currentPoint);
			double mutualReachabiltiyDistance = distance_get(&sc->distanceFunction, neighbor, currentPoint);
			//printf("5 -- hdbscan_construct_mst mutualReachabiltiyDistance = %f \n", mutualReachabiltiyDistance);

			if (coreDistances[currentPoint] > mutualReachabiltiyDistance) {
				mutualReachabiltiyDistance = coreDistances[currentPoint];
			}

			if (coreDistances[neighbor] > mutualReachabiltiyDistance) {
				mutualReachabiltiyDistance = coreDistances[neighbor];
			}

			double d;
			double_array_list_data(nearestMRDDistances, neighbor, &d);
			if (mutualReachabiltiyDistance < d) {
				double_array_list_set_value_at(nearestMRDDistances, mutualReachabiltiyDistance, neighbor);
				int_array_list_set_value_at(nearestMRDNeighbors, currentPoint, neighbor);
			}

			//Check if the unattached point being updated is the closest to the tree:
			double_array_list_data(nearestMRDDistances, neighbor, &d);
			if (d <= nearestMRDDistance) {
				nearestMRDDistance = d;
				nearestMRDPoint = neighbor;
			}
		}

		//Attach the closest point found in this iteration to the tree:
		attachedPoints[nearestMRDPoint] = TRUE;
		int_array_list_set_value_at(otherVertexIndices, numAttachedPoints, numAttachedPoints);
		currentPoint = nearestMRDPoint;
	}

	//If necessary, attach self edges:
	if (sc->selfEdges == TRUE) {
#pragma omp parallel for
		for (uint i = size - 1; i < size * 2 - 1; i++) {
			int vertex = i - (size - 1);
			int_array_list_set_value_at(nearestMRDNeighbors, vertex, i);
			int_array_list_set_value_at(otherVertexIndices, vertex, i);
			double_array_list_set_value_at(nearestMRDDistances, coreDistances[vertex], i);
		}
	}

	sc->mst = graph_init(NULL, size, nearestMRDNeighbors, otherVertexIndices, nearestMRDDistances);
	if(sc->mst == NULL){
		printf("Error: Could not initialise mst.\n");
		return HDBSCAN_ERROR;
	}

	return HDBSCAN_SUCCESS;
}

boolean hdbscan_propagate_tree(hdbscan* sc){

	set_t* clustersToExamine = set_init (sizeof(int32_t), int_compare);
	boolean addedToExaminationList[sc->clusters->size];
	boolean infiniteStability = FALSE;

#pragma omp parallel for
	for(size_t i = 0; i < sc->clusters->size; i++){
		addedToExaminationList[i] = FALSE;
	}

	for(size_t i = 0; i < sc->clusters->size; i++){

		cluster* cl;
		array_list_value_at(sc->clusters, i, &cl);
		if(cl != NULL && cl->hasChildren == FALSE){

			set_insert(clustersToExamine, &(cl->label));
			addedToExaminationList[cl->label] = TRUE;
		}
	}

	while(clustersToExamine->size > 0){
		
		int x;
		set_remove_at(clustersToExamine, clustersToExamine->size-1, &x);
		cluster* currentCluster = NULL;
		array_list_value_at(sc->clusters, x, &currentCluster);
		cluster_propagate(currentCluster);
		cluster* cl;
		array_list_value_at(sc->clusters, 1, &cl);

		if(currentCluster->stability == DBL_MAX){
			infiniteStability = TRUE;
		}
		if(currentCluster->parent != NULL){
			cluster *parent = currentCluster->parent;

			if(addedToExaminationList[parent->label] == FALSE){
				set_insert(clustersToExamine, &(parent->label));
				addedToExaminationList[parent->label] = TRUE;
			}
		}
		set_sort(clustersToExamine);
	}

	if(infiniteStability){
		const char *message =
					"----------------------------------------------- WARNING -----------------------------------------------\n"
					"(infinite) for some data objects, either due to replicates in the data (not a set) or due to numerical\n"
					"roundings. This does not affect the construction of the density-based clustering hierarchy, but\n"
					"it affects the computation of cluster stability by means of relative excess of mass. For this reason,\n"
					"the post-processing routine to extract a flat partition containing the most stable clusters may\n"
					"produce unexpected results. It may be advisable to increase the value of MinPts and/or M_clSize.\n"
					"-------------------------------------------------------------------------------------------------------";
		printf(message);
	}
	set_delete(clustersToExamine);

	return infiniteStability;
}

void hdbscan_find_prominent_clusters(hdbscan* sc, int infiniteStability){
	
	cluster* cl;
	array_list_value_at(sc->clusters, 1, &cl);
	ArrayList *solution = cl->propagatedDescendants;
	
	hashtable *significant = hashtable_init(array_list_size(solution), H_LONG, H_PTR, long_compare);
	int32_t ret;
	for(int32_t i = 0; i < solution->size; i++){
		cluster* c = NULL;
		ret = array_list_value_at(solution, i, &c);

		if(ret != 0){
			IntArrayList* clusterList = NULL;
			ret = hashtable_lookup(significant, &c->offset, &clusterList);

			if(clusterList == NULL){
				clusterList = int_array_list_init();
				long tmp = c->offset;
				hashtable_insert(significant, &tmp, &clusterList);
			}
			
			int_array_list_append(clusterList, c->label);
		}
	}
	
	sc->clusterLabels = (int32_t *)calloc(sc->numPoints, sizeof(int));

	for(size_t i = 0; i < set_size(significant->keys); i++)
	{
		int64_t key;
		set_value_at(significant->keys, i, &key);
		IntArrayList* clusterList = NULL;
		hashtable_lookup(significant, &key, &clusterList);
		hierarchy_entry* hpSecond;
		int64_t l = key + 1;
		
		hashtable_lookup(sc->hierarchy, &l, &hpSecond);

		for(int j = 0; j < sc->numPoints; j++){
			int label = (hpSecond->labels)[j];
			int it = int_array_list_search(clusterList, label);
			if(it != -1){
				sc->clusterLabels[j] = label;
			}
		}
	}

	hashtable_destroy(significant, NULL, array_list_delete);
}


int hdbscsan_calculate_outlier_scores(hdbscan* sc, double* pointNoiseLevels, int* pointLastClusters, boolean infiniteStability){

	double* coreDistances = sc->distanceFunction.coreDistances;
	int numPoints = sc->numPoints;
	sc->outlierScores = (outlier_score*)malloc(numPoints*sizeof(outlier_score));

	if(!sc->outlierScores){
		printf("ERROR: Calculate Outlier Score - Could not allocate memory for outlier scores.\n");
		return HDBSCAN_ERROR;
	}

	//int i = 0;
//#pragma omp parallel for
	for(uint i = 0; i < sc->numPoints; i++){
		int tmp = pointLastClusters[i];
		cluster* c;
		array_list_value_at(sc->clusters, tmp, &c);
		double epsilon_max = c->propagatedLowestChildDeathLevel;
		double epsilon = pointNoiseLevels[i];

		double score = 0;
		if(epsilon != 0){
			score = 1 - (epsilon_max / epsilon);
		}

		sc->outlierScores[i].id = i;
		sc->outlierScores[i].score = score;
		sc->outlierScores[i].coreDistance = coreDistances[i];
	}

	//Sort the outlier scores:
	qsort(sc->outlierScores, numPoints, sizeof(outlier_score), outlier_score_compare);

	return 1;
}

hashtable* hdbscan_create_cluster_map(int32_t* labels, int32_t begin, int32_t end){
	int32_t bsize = (end - begin)/4;
	hashtable* clusterTable = hashtable_init(bsize, H_INT, H_PTR, int_compare);

	printf("Table initialised from %d with %ld buckets", bsize, clusterTable->buckets);
	int32_t size = end - begin;

	for(int i = begin; i < end; i++){
		int *lb = labels + i;
		IntArrayList* clusterList = NULL;
		hashtable_lookup(clusterTable, lb, &clusterList);

		if(clusterList == NULL){
			clusterList = int_array_list_init_size(size/2);
			hashtable_insert(clusterTable, lb, &clusterList);
		}
		int_array_list_append(clusterList, i);
	}

	return clusterTable;
}

hashtable* hdbscan_get_min_max_distances(hdbscan* sc, hashtable* clusterTable){
	hashtable* distanceMap = hashtable_init(clusterTable->size, H_INT, H_PTR, int_compare);
	double* core = sc->distanceFunction.coreDistances;
	double zero = 0.0000000000000000000;
	int32_t key;
	for(size_t j = 0; j < set_size(clusterTable->keys); j++)
    {
        set_value_at(clusterTable->keys, j, &key);
		IntArrayList* clusterLabels = NULL;		
        hashtable_lookup(clusterTable, &key, &clusterLabels);
		int32_t* idxList = (int32_t* )clusterLabels->data;

		for(int i = 0; i < clusterLabels->size; i++){
			distance_values* dl = NULL;
			int32_t index = idxList[i];
			if(!hashtable_lookup(distanceMap, &key, &dl))
			{
				dl = (distance_values *)malloc(sizeof(distance_values));
				int32_t index = idxList[i];
				dl->min_cr = core[index];
				dl->max_cr = core[index];
				dl->cr_confidence = 0.0;

				dl->min_dr = DBL_MAX;
				dl->max_dr = DBL_MIN;
				dl->dr_confidence = 0.0;
				hashtable_insert(distanceMap, &key, &dl);
			} else {

				if(dl->min_cr > core[index] && (core[index] < zero || core[index] > zero)){
					dl->min_cr = core[index];
				}

				//max core distance
				if(dl->max_cr < core[index]){
					dl->max_cr = core[index];
				}
			}
			// Calculating min and max distances
			for(size_t k = j+1; k < clusterLabels->size; k++){
				double d = distance_get(&(sc->distanceFunction), index, idxList[k]);

				if(dl->min_dr > d && (d < zero || d > zero)){
					dl->min_dr = d;
				}

				// max distance
				if(dl->max_dr < d){
					dl->max_dr = d;
				}
			}
		}
    }

	return distanceMap;
}

void hdbscan_destroy_distance_map(hashtable* table){

    hashtable_destroy(table, NULL, int_array_list_delete);
	table = NULL;
}

/**
 * Calculate skewness and kurtosis using the equations from 
 * https://www.gnu.org/software/gsl/doc/html/statistics.html
 */ 
void hdbscan_skew_kurt_1(clustering_stats* stats, double sum_sc, double sum_sd, double sum_dc, double sum_dd)
{
	int32_t N = stats->count;
	stats->coreDistanceValues.skewness = sum_sc / (N * pow(stats->coreDistanceValues.standardDev, 3));
	stats->intraDistanceValues.skewness = sum_sd / (N * pow(stats->intraDistanceValues.standardDev, 3));

	stats->coreDistanceValues.kurtosis = (sum_dc / (N * pow(stats->coreDistanceValues.standardDev, 4))) - 3;
	stats->intraDistanceValues.kurtosis = (sum_dd / (N *pow(stats->intraDistanceValues.standardDev, 4))) - 3;
}

/**
 * Calculate skewness and kurtosis in the same way as in MS excel
 * https://support.office.com/en-us/article/skew-function-bdf49d86-b1ef-4804-a046-28eaea69c9fa
 * https://support.office.com/en-ie/article/kurt-function-bc3a265c-5da4-4dcb-b7fd-c237789095ab
 */ 
void hdbscan_skew_kurt_2(clustering_stats* stats, double sum_sc, double sum_sd, double sum_dc, double sum_dd)
{

	int32_t N = stats->count;
	
	// Calculate the skewness
	if(stats->count >= 2){
		double tmp1 = ((double)N) / ((N - 1) * (N - 2));
		stats->coreDistanceValues.skewness = tmp1 * (sum_sc / pow(stats->coreDistanceValues.standardDev, 3));
		stats->intraDistanceValues.skewness = tmp1 * (sum_sd / pow(stats->intraDistanceValues.standardDev, 3));
	} else {
		stats->coreDistanceValues.skewness = 0.0/0.0;
		stats->intraDistanceValues.skewness = 0.0/0.0;
	}

	// Calculate the kurtosis
	if(stats->count >= 3){
		double tmp2 = (((double)N) * (N + 1)) / ((N - 1) * (N - 2) * (N - 3));
		stats->coreDistanceValues.kurtosis = tmp2 * (sum_dc / pow(stats->coreDistanceValues.standardDev, 4));
		stats->intraDistanceValues.kurtosis = tmp2 * (sum_dd / pow(stats->intraDistanceValues.standardDev, 4));
	}

	if(stats->count >= 3){
		double tmp3 = (3 * (((double)N) - 1) * (N - 1)) / ((N - 2) * (N - 3));
		stats->coreDistanceValues.kurtosis -= tmp3;
		stats->intraDistanceValues.kurtosis -= tmp3;
	} else {

		stats->coreDistanceValues.kurtosis = 0.0/0.0;
		stats->intraDistanceValues.kurtosis = 0.0/0.0;
	}
}

/**
 * Calculate the skewness and kurtosis using the the gsl library.
 * 
 * https://www.gnu.org/software/gsl/doc/html/statistics.html
 */ 
void hdbscan_skew_kurt_gsl(clustering_stats* stats, double* cr, double* dr)
{
	/*
	stats->coreDistanceValues.standardDev = gsl_stats_sd(cr, 1, stats->count);
	stats->coreDistanceValues.variance = gsl_stats_variance(cr, 1, stats->count);
	stats->coreDistanceValues.mean = gsl_stats_mean(cr, 1, stats->count);
	stats->coreDistanceValues.kurtosis = gsl_stats_kurtosis_m_sd(cr, 1, stats->count, stats->coreDistanceValues.mean, stats->coreDistanceValues.standardDev);
	stats->coreDistanceValues.skewness = gsl_stats_skew_m_sd(cr, 1, stats->count, stats->coreDistanceValues.mean, stats->coreDistanceValues.standardDev);
	stats->coreDistanceValues.max = gsl_stats_max(cr, 1, stats->count);

	// calculating intra distance statistics
	stats->intraDistanceValues.standardDev = gsl_stats_sd(dr, 1, stats->count);
	stats->intraDistanceValues.variance = gsl_stats_variance(dr, 1, stats->count);
	stats->intraDistanceValues.mean = gsl_stats_mean(dr, 1, stats->count);
	stats->intraDistanceValues.kurtosis = gsl_stats_kurtosis_m_sd(dr, 1, stats->count, stats->coreDistanceValues.mean, stats->coreDistanceValues.standardDev);
	stats->intraDistanceValues.skewness = gsl_stats_skew_m_sd(dr, 1, stats->count, stats->coreDistanceValues.mean, stats->coreDistanceValues.standardDev);
	stats->intraDistanceValues.max = gsl_stats_max(dr, 1, stats->count);
	*/
}

/**
 * 
 * 
 */ 
void hdbscan_calculate_stats_helper(double* cr, double* dr, clustering_stats* stats){
	
	stats->coreDistanceValues.mean = cr[0];
	stats->coreDistanceValues.max = cr[0];

	stats->intraDistanceValues.mean = dr[0];
	stats->intraDistanceValues.max = dr[0];

	for(int32_t i = 1; i < stats->count; i++)
	{
		// Core distance statistics
		if(cr[i] > stats->coreDistanceValues.max)
		{
			stats->coreDistanceValues.max = cr[i];
		}

		stats->coreDistanceValues.mean += cr[i];

		// Intra cluster distances
		if(dr[i] > stats->intraDistanceValues.max)
		{
			stats->intraDistanceValues.max = dr[i];
		}

		stats->intraDistanceValues.mean += dr[i];
	}

	// Get the average
	stats->coreDistanceValues.mean = stats->coreDistanceValues.mean/stats->count;
	stats->intraDistanceValues.mean = stats->intraDistanceValues.mean/stats->count;

	/// Calculate the variance
	double sum_sc = 0;
	double sum_sd = 0;
	double sum_dc = 0;
	double sum_dd = 0;
	
	for(int32_t i = 0; i < stats->count; i++)
	{
		double tmp_c = cr[i] - stats->coreDistanceValues.mean;
		stats->coreDistanceValues.variance += tmp_c * tmp_c;
		sum_sc += pow(tmp_c, 3);
		sum_dc += pow(tmp_c, 4);

		double tmp_d = dr[i] - stats->intraDistanceValues.mean;
		stats->intraDistanceValues.variance += tmp_d * tmp_d;
		sum_sd += pow(tmp_d, 3);
		sum_dd += pow(tmp_d, 4);
	}

	stats->coreDistanceValues.variance = stats->coreDistanceValues.variance / (stats->count - 1);
	stats->intraDistanceValues.variance = stats->intraDistanceValues.variance / (stats->count - 1);

	stats->coreDistanceValues.standardDev = sqrt(stats->coreDistanceValues.variance);
	stats->intraDistanceValues.standardDev = sqrt(stats->intraDistanceValues.variance);

	//hdbscan_skew_kurt_1(stats, sum_sc, sum_sd, sum_dc, sum_dd);
	hdbscan_skew_kurt_2(stats, sum_sc, sum_sd, sum_dc, sum_dd);
	//hdbscan_skew_kurt_gsl(stats, cr, dr);
	
}

void hdbscan_calculate_stats(hashtable* distanceMap, clustering_stats* stats){

	double cr[hashtable_size(distanceMap)];
	double dr[hashtable_size(distanceMap)];

	for(size_t i = 0; i < hashtable_size(distanceMap); i++)
	{
		int32_t key;
		distance_values* dl = NULL;
		set_value_at(distanceMap->keys, i, &key);
		hashtable_lookup(distanceMap, &key, &dl);
		cr[i] = dl->max_cr/dl->min_cr;
		dr[i] = dl->max_dr/dl->min_dr;
	}

	stats->count = hashtable_size(distanceMap);
	hdbscan_calculate_stats_helper(cr, dr, stats);

	for(size_t i = 0; i < hashtable_size(distanceMap); i++)
	{
		int32_t key;
		distance_values* dl = NULL;
		set_value_at(distanceMap->keys, i, &key);
		hashtable_lookup(distanceMap, &key, &dl);
		double rc = cr[i];
		double rd = dr[i];

		dl->cr_confidence = ((stats->coreDistanceValues.max - rc) / stats->coreDistanceValues.max) * 100;
		dl->dr_confidence = ((stats->intraDistanceValues.max - rd) / stats->intraDistanceValues.max) * 100;
	}
}

int32_t hdbscan_analyse_stats(clustering_stats* stats){
	int32_t validity = 0;

	double skew_cr = stats->coreDistanceValues.skewness;
	double skew_dr = stats->intraDistanceValues.skewness;
	double kurtosis_cr = stats->coreDistanceValues.kurtosis;
	double kurtosis_dr = stats->intraDistanceValues.kurtosis;

	if((skew_dr > 0.0 ) && (kurtosis_dr > 0.0 )){
		validity += 2;
	} else if(skew_dr < 0.0 && kurtosis_dr > 0.0){
		validity += 1;
	} else if(skew_dr > 0.0 && kurtosis_dr < 0.0){
		validity += 0;
	} else{
		validity += -1;
	}

	if((skew_cr > 0.0 ) && (kurtosis_cr > 0.0 )){
		validity += 2;
	} else if(skew_cr < 0.0 && kurtosis_cr > 0.0){
		validity += 1;
	} else if(skew_cr > 0.0 && kurtosis_cr < 0.0){
		validity += 0;
	} else{
		validity += -1;
	}

	return validity;
}

void partition(int32_t *c_data, double *d_data, int lower, int upper, int* pivot){
	double x = d_data[lower];
	int32_t x2 = c_data[lower];

	int up = lower+1; /* index will go up */
	int down = upper; /* index will go down */

	while(up < down){
		while((up < down) && (d_data[up] <= x)) {
			up++;
		}

		while((up < down) && (d_data[down] > x)){
			down--;
		}

		if(up == down){
			break;
		}

		double tmp = d_data[up];
		d_data[up] = d_data[down];
		d_data[down] = tmp;

		int32_t tmp2 = c_data[up];
		c_data[up] = c_data[down];
		c_data[down] = tmp2;

	}
	if(d_data[up] > x){
		up--;
	}

	d_data[lower] = d_data[up];
	d_data[up] = x;

	c_data[lower] = c_data[up];
	c_data[up] = x2;

	*pivot = up;
}

void hdbscan_quicksort(IntArrayList *clusters, DoubleArrayList *sortData, int32_t left, int32_t right){

	double *d_data = (double *)sortData->data;
	int32_t *c_data = (int32_t *)clusters->data;
	if(left < right){
		int32_t pivot;
		partition(c_data, d_data, left, right, &pivot);
		hdbscan_quicksort(clusters, sortData, left, pivot - 1);
		hdbscan_quicksort(clusters, sortData, pivot + 1, right);
	}
}

/**
 * Sorts the clusters using the distances in the distanceMap. This
 * function requires that the confidences be already calculates. This
 * can be achieved by calling the hdbscan_calculate_stats function
 */
IntArrayList* hdbscan_sort_by_similarity(hashtable* distanceMap, IntArrayList *clusters, int32_t distanceType){
	
	assert(distanceMap != NULL); 
	DoubleArrayList *distances;
	if(clusters == NULL){
		clusters = int_array_list_init_size(hashtable_size(distanceMap));
		distances = double_array_list_init_size(hashtable_size(distanceMap));
	} else{
		distances = double_array_list_init_exact_size(clusters->size);
	}

	int32_t size = clusters->size;

	if(size == 0){     /// If clusters had nothing in it, we will use the whole hash table

		for(size_t i = 0; i < hashtable_size(distanceMap); i++)
		{
			int32_t key;
			distance_values* dv = NULL;
			set_value_at(distanceMap->keys, i, &key);
			hashtable_lookup(distanceMap, &key, &dv);
		
			int_array_list_append(clusters, key);
			double conf;

			if(distanceType == CORE_DISTANCE_TYPE){
				conf = dv->cr_confidence;
			} else{
				conf = dv->dr_confidence;
			}

			double_array_list_append(distances, conf);
		
		}

	} else { /// else we just need to get the lengths from the hash table
		int32_t *data = (int32_t *)clusters->data;
		distances->size = clusters->size;
		double *ddata = (double *)distances->data;

#pragma omp parallel for
		for(int32_t i = 0; i < clusters->size; i++){
			int32_t *key = data + i;
			distance_values *dv = NULL;
			hashtable_lookup(distanceMap, key, &dv);
			double conf;

			if(distanceType == CORE_DISTANCE_TYPE){
				conf = dv->cr_confidence;
			} else{
				conf = dv->dr_confidence;
			}
			ddata[i] = conf;
		}
	}

	// sort
	hdbscan_quicksort(clusters, distances, 0, clusters->size-1);
	double_array_list_delete(distances);

	return clusters;
}

/**
 * Sorts clusters according to how long the cluster is
 */
IntArrayList* hdbscan_sort_by_length(hashtable* clusterTable, IntArrayList *clusters){

	assert(clusterTable != NULL && clusters != NULL);
	DoubleArrayList *lengths;
	if(clusters == NULL){
		clusters = int_array_list_init_size(hashtable_size(clusterTable));
		lengths = double_array_list_init_size(hashtable_size(clusterTable));
		
	} else{
		lengths = double_array_list_init_exact_size(clusters->size);
	}
	
	int32_t size = clusters->size;

	if(size == 0){     /// If clusters had nothing in it, we will use the whole hash table
		for(size_t i = 0; i < hashtable_size(clusterTable); i++)
		{
			int32_t key;
			IntArrayList *lst = NULL;
			set_value_at(clusterTable->keys, i, &key);
			hashtable_lookup(clusterTable, &key, &lst);
			int_array_list_append(clusters, key);
			double_array_list_append(lengths, (double)lst->size);
		}

	} else { /// else we just need to get the lengths from the hash table
		int32_t *data = (int32_t *)clusters->data;
		//lengths->size = clusters->size;
		//double *ddata = (double *)lengths->data;
		
#pragma omp parallel for
		for(int32_t i = 0; i < clusters->size; i++){
			int32_t *key = data + i;
			IntArrayList *lst = NULL;
			hashtable_lookup(clusterTable, key, &lst);
			double_array_list_append(lengths, (double)lst->size);
		}
		
	}
	// sort
//#pragma omp barrier
	hdbscan_quicksort(clusters, lengths, 0, clusters->size-1);
	double_array_list_delete(lengths);
	return clusters;
}

/**
 * Destroys the cluster table
 */
void hdbscan_destroy_cluster_map(hashtable* table){

	assert(table != NULL);

	/*for(size_t i = 0; i < hashtable_size(clusterTable); i++)
	{
		int32_t key;
		IntArrayList *lst = NULL;
		set_value_at(distanceMap->keys, i, &key);
		hashtable_lookup(table, &key, &lst);
		int_array_list_delete(lst);
	}*/
	hashtable_destroy(table, NULL, int_array_list_delete);
	table = NULL;
}

void hdbscan_print_cluster_map(hashtable* table){

	assert(table != NULL);

	for(size_t i = 0; i < hashtable_size(table); i++)
	{
		int32_t key;
		IntArrayList *clusterList = NULL;
		set_value_at(table->keys, i, &key);
		hashtable_lookup(table, &key, &clusterList);
		printf("%d -> [", key);

		for(int j = 0; j < clusterList->size; j++){
			int32_t dpointer;
			int_array_list_data(clusterList, j, &dpointer);
			printf("%d ", dpointer);
		}
		printf("]\n");
	}
}

void hdbscan_print_cluster_sizes(hashtable* table){

	assert(table != NULL);
	for(size_t i = 0; i < hashtable_size(table); i++)
	{
		int32_t key;
		IntArrayList *clusterList = NULL;
		set_value_at(table->keys, i, &key);
		hashtable_lookup(table, &key, &clusterList);
		printf("%d : %ld\n", key, clusterList->size);
	}
}

void hdbscan_print_hierarchies(hashtable* hierarchy, uint numPoints, char *filename){

	assert(hierarchy != NULL && filename != NULL);
	FILE *visFile = NULL;
	FILE *hierarchyFile = NULL;

	/// TODO: Use strcat_s
	if(filename != NULL){

		char visFilename[100];
		strcat(visFilename, filename);
		strcat(visFilename, "_visualization.vis");
		visFile = fopen(visFilename, "w");
		fprintf(visFile, "1\n");
		fprintf(visFile, "%ld\n", hashtable_size(hierarchy));
		fclose(visFile);

		char hierarchyFilename[100];
		strcat(hierarchyFilename, filename);
		strcat(hierarchyFilename, "_hierarchy.csv");
		hierarchyFile = fopen(hierarchyFilename, "w");
	}

	printf("\n/////////////////////////////////Printing Hierarchies//////////////////////////////////////////////////////\n");
	printf("hierarchy size = %ld\n", hashtable_size(hierarchy));
	
	for(size_t i = 0; i < set_size(hierarchy->keys); i++){
		int64_t level; // = *((int64_t *)key);
		hierarchy_entry* data;// = (hierarchy_entry*)value;
		set_value_at(hierarchy->keys, i, &level);
        hashtable_lookup(hierarchy, &level, &data);

		if(hierarchyFile){
			fprintf(hierarchyFile, "%.15f,", data->edgeWeight);
		} else {
			printf("%ld : %.15f -> [", level, data->edgeWeight);
		}

		for(int j = 0; j < numPoints; j++){
			if(hierarchyFile){
				fprintf(hierarchyFile, "%d,", data->labels[j]);
			} else {
				printf("%d ", data->labels[j]);
			}
		}

		if(hierarchyFile){
			fprintf(hierarchyFile, "\n");
		} else {
			printf("]\n");
		}
	}

	if(hierarchyFile){
		fclose(hierarchyFile);
	}
	printf("///////////////////////////////////////////////////////////////////////////////////////\n\n");
}

void hdbscan_print_distance_map(hashtable* distancesMap){

	printf("\n/////////////////////////////////Printing Distances//////////////////////////////////////////////////////\n");
	for(size_t i = 0; i < hashtable_size(distancesMap); i++)
	{
		int32_t key;
		distance_values* dv = NULL;
		set_value_at(distancesMap->keys, i, &key);
		hashtable_lookup(distancesMap, &key, &dv);

		printf("min_cr : %f, max_cr : %f, cr_confidence : %f\n", dv->min_cr, dv->max_cr, dv->cr_confidence);
		printf("min_dr : %f, max_dr : %f, dr_confidence : %f\n", dv->min_dr, dv->max_dr, dv->dr_confidence);

		printf("}\n\n");
	}
	printf("///////////////////////////////////////////////////////////////////////////////////////\n\n");
}

void hdbscan_print_stats(clustering_stats* stats){

	printf("//////////////////////////////////////// Statistical Values ///////////////////////////////////////////////\n");

	printf("Cluster Count 					: %d\n", stats->count);
	printf("Core Distances - Max 			: %.5f\n", stats->coreDistanceValues.max);
	printf("Core Distances - Mean 			: %.5f\n", stats->coreDistanceValues.mean);
	printf("Core Distances - Skewness 		: %.5f\n", stats->coreDistanceValues.skewness);
	printf("Core Distances - Kurtosis 		: %.5f\n", stats->coreDistanceValues.kurtosis);
	printf("Core Distances - Variance 		: %.5f\n", stats->coreDistanceValues.variance);
	printf("Core Distances - Standard Dev 	: %.5f\n\n", stats->coreDistanceValues.standardDev);

	printf("Intra Distances - Max 			: %.5f\n", stats->intraDistanceValues.max);
	printf("Intra Distances - Mean 			: %.5f\n", stats->intraDistanceValues.mean);
	printf("Intra Distances - Skewness 		: %.5f\n", stats->intraDistanceValues.skewness);
	printf("Intra Distances - Kurtosis 		: %.5f\n", stats->intraDistanceValues.kurtosis);
	printf("Intra Distances - Variance 		: %.5f\n", stats->intraDistanceValues.variance);
	printf("Intra Distances - Standard Dev 	: %.5f\n", stats->intraDistanceValues.standardDev);

	printf("///////////////////////////////////////////////////////////////////////////////////////\n\n");
}

void hdbscan_destroy_hierarchical_entry(hierarchy_entry* entry)
{
	if(entry)
	{
		free(entry->labels);
		free(entry);
	}
}