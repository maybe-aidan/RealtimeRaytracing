#ifndef RT_BVH_H
#define RT_BVH_H

#include "rt_structs.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <glm/glm/glm.hpp>

// Axis-Aligned Bounding Box
struct AABB {
	glm::vec3 min, max;

	AABB() : min(FLT_MAX), max(-FLT_MAX) {}
	AABB(glm::vec3 min, glm::vec3 max) : min(min), max(max) {}

	void expand(const glm::vec3& point) {
		min = glm::min(min, point);
		max = glm::max(max, point);
	}

	void expand(const AABB& other) {
		min = glm::min(min, other.min);
		max = glm::max(max, other.max);
	}

	float surfaceArea() const {
		glm::vec3 d = max - min;
		return 2.0f * (d.x * d.y + d.y * d.z + d.x * d.z);
	}

	glm::vec3 center() const {
		return (min + max) * 0.5f;
	}
};


// A BVH is an acceleration structure that recursively splits a mesh or 
// scene into smaller nodes containing less geometry. The idea here is 
// to be able to discard as much geometry as possble per ray, in order 
// to perform as few intersection tests as possible in the shader.
class BVHBuilder {
public:
	std::vector<BVHNode> nodes;
	std::vector<int> primitiveIndices; // The index into the triangle array.
	
	void build(const std::vector<Triangle>& triangles) {
		nodes.clear();
		primitiveIndices.clear();

#ifdef RT_DEBUG
		std::cout << "BVH build called with " << triangles.size() << " triangles" << std::endl;
#endif
		if (triangles.empty()) {
			std::cout << "No triangles to build BVH for!" << std::endl;
			return;
		}

		std::vector<PrimInfo> primInfo;
		primInfo.reserve(triangles.size());

		for (size_t i = 0; i < triangles.size(); i++) {
			AABB bounds = getTriangleBounds(triangles[i]);
			primInfo.push_back({ bounds, bounds.center(), (int)i });
		}

		nodes.reserve(2 * triangles.size());
		primitiveIndices.reserve(triangles.size());

		buildRecursive(primInfo, 0, primInfo.size());
#ifdef RT_DEBUG
		std::cout << "BVH built with " << nodes.size() << " nodes for "
			<< triangles.size() << " triangles" << std::endl;
#endif
	}

	const std::vector<BVHNode>& getNodes() const { return nodes; }
	const std::vector<int>& getPrimitiveIndices() const { return primitiveIndices; }

	void refit(const std::vector<Triangle>& triangles) {
		if (nodes.empty()) return;
		refitNode(0, triangles);
	}

private:
	struct PrimInfo {
		AABB bounds;
		glm::vec3 centroid;
		int primitiveIndex;
	};

	static constexpr int MAX_PRIMS_IN_LEAF = 4;

	AABB refitNode(int nodeIdx, const std::vector<Triangle>& triangles) {
		BVHNode& node = nodes[nodeIdx];

		if (node.leftChild < 0) { // leaf
			const int primOffset = -node.leftChild - 1;
			const int primCount = node.rightChild;

			AABB bounds;
			for (int i = 0; i < primCount; ++i) {
				const int triIdx = primitiveIndices[primOffset + i];
				// Reuse same bounding method used at build-time:
				const Triangle& t = triangles[triIdx];

				// Inline getTriangleBounds(t) to avoid private access issues:
				AABB tb;
				tb.expand(glm::vec3(t.v0.x, t.v0.y, t.v0.z));
				tb.expand(glm::vec3(t.v1.x, t.v1.y, t.v1.z));
				tb.expand(glm::vec3(t.v2.x, t.v2.y, t.v2.z));

				bounds.expand(tb);
			}
			node.min = glm::vec4(bounds.min, 0.0f);
			node.max = glm::vec4(bounds.max, 0.0f);
			return bounds;
		}
		else {
			// internal: combine children
			const int L = node.leftChild;
			const int R = node.rightChild;
			AABB leftB = refitNode(L, triangles);
			AABB rightB = refitNode(R, triangles);

			AABB b = leftB;
			b.expand(rightB);
			node.min = glm::vec4(b.min, 0.0f);
			node.max = glm::vec4(b.max, 0.0f);
			return b;
		}
	}

	AABB getTriangleBounds(const Triangle& tri) const {
		AABB bounds;
		bounds.expand(tri.v0);
		bounds.expand(tri.v1);
		bounds.expand(tri.v2);
		return bounds;
	}

	int buildRecursive(std::vector<PrimInfo>& primInfo, int start, int end) {
		int nodeIndex = nodes.size();
		nodes.emplace_back(); // Placeholder

		// Compute bounding box for current range
		AABB bounds;
		for (int i = start; i < end; i++) {
			bounds.expand(primInfo[i].bounds);
		}

		int numPrims = end - start;

		if (numPrims <= MAX_PRIMS_IN_LEAF) {
			createLeaf(nodeIndex, bounds, primInfo, start, end);
			return nodeIndex;
		}

		// Choose split dimension and position using SAH
		int splitDim;
		int splitPos;
		float splitCost = findBestSplit(primInfo, start, end, bounds, splitDim, splitPos);

		// Create a leaf if the split isn't beneficial
		if (splitCost >= numPrims) {
			createLeaf(nodeIndex, bounds, primInfo, start, end);
			return nodeIndex;
		}

		// Partition primitives
		std::nth_element(primInfo.begin() + start, 
			primInfo.begin() + splitPos, 
			primInfo.begin() + end,
			[splitDim](const PrimInfo& a, const PrimInfo& b) {
				return a.centroid[splitDim] < b.centroid[splitDim];
			}
		);

		// Build children
		int leftChild = buildRecursive(primInfo, start, splitPos);
		int rightChild = buildRecursive(primInfo, splitPos, end);

		// Update current
		BVHNode& node = nodes[nodeIndex];
		node.min = glm::vec4(bounds.min, 0.0);
		node.max = glm::vec4(bounds.max, 0.0);
		node.leftChild = leftChild;
		node.rightChild = rightChild;
		node.pad0 = node.pad1 = 0;

		return nodeIndex;
	}

	void createLeaf(int nodeIndex, const AABB& bounds, const std::vector<PrimInfo>& primInfo, int start, int end) {
		BVHNode& node = nodes[nodeIndex];
		node.min = glm::vec4(bounds.min, 0.0);
		node.max = glm::vec4(bounds.max, 0.0);

		// Store current size before adding primitives
		int primitiveOffset = (int)primitiveIndices.size();

		// negative values indicate leaf nodes
		node.leftChild = -(primitiveOffset+ 1); // negative offset to primitives. +1 is to ensure that we are non-zero.
		node.rightChild = end - start;

		// add primitives to the array
		for (int i = start; i < end; i++) {
			int ref;
			ref = primInfo[i].primitiveIndex;
			primitiveIndices.push_back(ref);
		}
	}

	float findBestSplit(std::vector<PrimInfo>& primInfo, int start, int end,
		const AABB& bounds, int& bestDim, int& bestPos) {
		float bestCost = FLT_MAX;
		bestDim = 0;
		bestPos = (start + end) / 2;

		const int numBuckets = 12;

		// Try each dimension
		for (int dim = 0; dim < 3; dim++) {
			// Find centroid bounds for this dimension
			float minCentroid = FLT_MAX;
			float maxCentroid = -FLT_MAX;

			for (int i = start; i < end; i++) {
				minCentroid = std::min(minCentroid, primInfo[i].centroid[dim]);
				maxCentroid = std::max(maxCentroid, primInfo[i].centroid[dim]);
			}

			if (minCentroid == maxCentroid) continue; // Can't split on this dimension

			// Initialize buckets
			struct Bucket {
				int count = 0;
				AABB bounds;
			};
			Bucket buckets[numBuckets];

			// Assign primitives to buckets
			for (int i = start; i < end; i++) {
				int bucketIndex = std::min(numBuckets - 1,
					(int)(numBuckets * (primInfo[i].centroid[dim] - minCentroid) / (maxCentroid - minCentroid)));
				buckets[bucketIndex].count++;
				buckets[bucketIndex].bounds.expand(primInfo[i].bounds);
			}

			// Compute cost for each potential split
			for (int splitBucket = 1; splitBucket < numBuckets; splitBucket++) {
				AABB leftBounds, rightBounds;
				int leftCount = 0, rightCount = 0;

				// Left side (buckets 0 to splitBucket-1)
				for (int i = 0; i < splitBucket; i++) {
					leftBounds.expand(buckets[i].bounds);
					leftCount += buckets[i].count;
				}

				// Right side (buckets splitBucket to numBuckets-1)
				for (int i = splitBucket; i < numBuckets; i++) {
					rightBounds.expand(buckets[i].bounds);
					rightCount += buckets[i].count;
				}

				// Skip if one side is empty
				if (leftCount == 0 || rightCount == 0) continue;

				// SAH cost: traversal cost + intersection cost
				float traversalCost = 0.125f;
				float intersectionCost = 1.0f;
				float cost = traversalCost + intersectionCost *
					(leftCount * leftBounds.surfaceArea() +
						rightCount * rightBounds.surfaceArea()) / bounds.surfaceArea();

				if (cost < bestCost) {
					bestCost = cost;
					bestDim = dim;

					// Calculate split centroid
					float splitCentroid = minCentroid + (maxCentroid - minCentroid) * splitBucket / numBuckets;

					// Find actual split position by partitioning
					auto splitIter = std::partition(primInfo.begin() + start,
						primInfo.begin() + end,
						[dim, splitCentroid](const PrimInfo& p) {
							return p.centroid[dim] < splitCentroid;
						});
					bestPos = splitIter - primInfo.begin();

					// Ensure split position is valid
					bestPos = std::max(start + 1, std::min(end - 1, bestPos));
				}
			}
		}

		return bestCost;
	}

};

#endif