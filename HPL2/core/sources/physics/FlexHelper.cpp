#include "physics/FlexHelper.h"
#include "graphics/VertexBuffer.h"
#include "system/LowLevelSystem.h"
#include "impl/LowLevelGraphicsSDL.h"
#include "math/Math.h"
#include <array>

namespace hpl {

	cFlexHelper::cFlexHelper()
	{

	}

	cFlexHelper::~cFlexHelper()
	{

	}

	void cFlexHelper::PresetParameters(NvFlexParams& params, int alType)
	{
		switch (alType) {
		case ePresetType_GranularPile:
			params.numIterations			= 12;								// Num Iterations
			params.radius					= 0.075f;							// Radius
			params.solidRestDistance		= params.radius;					// Solid Radius
			params.fluidRestDistance		= 0;								// Fluid Radius
			params.dynamicFriction			= 0.5f;								// Dynamic Friction
			params.staticFriction			= 1.0f;								// Static Friction
			params.particleFriction			= 0.05f;							// Particle Friction
			params.restitution				= 0.2f;								// Restitution
			params.sleepThreshold			= params.radius * 0.25f;			// Sleep Threshold
			params.shockPropagation			= 6.0f;								// Shock Propogation
			params.damping					= 0.14f;							// Damping
			params.dissipation				= 0;								// Dissapation
			params.relaxationFactor			= 1.0f;								// SOR
			params.collisionDistance		= params.radius * 0.5f;				// Collision Distance
			params.shapeCollisionMargin		= params.collisionDistance * 0.5f;	// Collision Margin
			params.viscosity				= 0.0f;								// Viscosity
			params.particleCollisionMargin	= params.radius * 0.25f;			// 5% collision margin
			break;
		}
	}

	int cFlexHelper::CreateParticleCube(SimBuffers* apSimBuffers, cVector3f avPos, int alSize, float afSpacing, int alMaxParticles)
	{
		const int lParticleCount = alSize * alSize * alSize;
		if (alMaxParticles < lParticleCount) return -1;

		MapBuffers(apSimBuffers);

		apSimBuffers->positions.resize(lParticleCount);
		apSimBuffers->velocities.resize(lParticleCount);
		apSimBuffers->phases.resize(lParticleCount);
		apSimBuffers->activeIndices.resize(lParticleCount);

		Vec3 vPos = Vec3(avPos.x, avPos.y, avPos.z) - 0.5f * Vec3((alSize - 1) * afSpacing, (alSize - 1) * afSpacing, (alSize - 1) * afSpacing);
		const float fInvMass = 1.0f;
		const Vec3 vInitialVel = Vec3(0.0f);
		const int lPhase = NvFlexMakePhase(0, eNvFlexPhaseSelfCollide);
		uint32_t lParticleIdx = 0;

		for (int z = 0; z < alSize; ++z) {
			for (int y = 0; y < alSize; ++y) {
				for (int x = 0; x < alSize; ++x)
				{
					float px = vPos.x + x * afSpacing;
					float py = vPos.y + y * afSpacing;
					float pz = vPos.z + z * afSpacing;
					Vec4 vParticlePos = Vec4(px, py, pz, fInvMass);

					apSimBuffers->positions[lParticleIdx]		= vParticlePos;
					apSimBuffers->velocities[lParticleIdx]		= vInitialVel;
					apSimBuffers->phases[lParticleIdx]			= lPhase;
					apSimBuffers->activeIndices[lParticleIdx]	= lParticleIdx;

					lParticleIdx++;
				}
			}
		}

		// Sanity check
		if (lParticleIdx != lParticleCount || lParticleIdx != apSimBuffers->positions.size())
			FatalError("Creation of particle cube failed!\n\nFinal Index: %d\nExpected Count: %d\nActual Count: %d", lParticleIdx, lParticleCount, apSimBuffers->positions.size());

		UnmapBuffers(apSimBuffers);
		return lParticleIdx;
	}

	void cFlexHelper::AddRigidParticleBody(cSubMeshEntity* apSubMeshEntity, NvFlexLibrary* apFlexLibrary, NvFlexSolver* apFlexSolver, SimBuffers* apSimBuffers, float afParticleRadius, float afShellOnly, float afRigidStiffness)
	{
		// --- Get Vertex Buffer and check validity ---
		iVertexBuffer* vb = apSubMeshEntity->GetVertexBuffer();
		if (!vb) {
			Warning("AddRigidParticleBody: No vertex buffer found for entity '%s'.", apSubMeshEntity->GetName().c_str());
			return;
		}

		const int vCount = vb->GetVertexNum();
		const int iCount = vb->GetIndexNum();
		if (!vCount || !iCount) {
			Warning("AddRigidParticleBody: Vertex or Index count is zero for entity '%s'.", apSubMeshEntity->GetName().c_str());
			return;
		}

		// --- Get Vertex and Index Data Pointers ---
		const float* srcPos4 = vb->GetFloatArray(eVertexBufferElement_Position);
		if (!srcPos4) {
			Error("AddRigidParticleBody: Vertex buffer missing position data (eVertexBufferElement_Position) for entity '%s'.", apSubMeshEntity->GetName().c_str());
			return;
		}
		const void* srcIndexPtr = vb->GetIndices();
		if (!srcIndexPtr) {
			Error("AddRigidParticleBody: Vertex buffer missing index data for entity '%s'.", apSubMeshEntity->GetName().c_str());
			return;
		}

		// --- Copy Index Data (uint32_t) ---
		std::vector<uint32_t> tris(iCount);
		// Ensure the source index format matches uint32_t or cast appropriately during copy if needed
		memcpy(tris.data(), srcIndexPtr, sizeof(uint32_t) * iCount);

		// --- Get World Transform ---
		cMatrixf worldTransform = apSubMeshEntity->GetWorldMatrix();

		// --- Transform Local Vertices to World Space ---
		std::vector<Vec3> worldPositions(vCount);
		for (int i = 0; i < vCount; ++i) {
			cVector3f localP(srcPos4[i * 4 + 0], srcPos4[i * 4 + 1], srcPos4[i * 4 + 2]);
			cVector3f worldP = cMath::MatrixMul(worldTransform, localP);
			worldPositions[i] = Vec3(worldP.x, worldP.y, worldP.z);
		}

		// --- Direct Sampling (World Space) ---
		std::vector<Vec3> initialSamples; // World-space samples before clustering

		// 1. Add original vertices as samples
		for (const auto& p : worldPositions) {
			initialSamples.push_back(p);
		}

		// 2. Add random triangle samples
		const int numRandomSamples = 50000; // Adjust as needed
		RandInit(); // Initialize random seed if not done globally
		for (int i = 0; i < numRandomSamples; ++i)
		{
			if (tris.empty()) break;
			int tIdx = Rand() % (tris.size() / 3); // Index of the triangle
			float u = Randf();
			float v = Randf() * (1.0f - u);
			float w = 1.0f - u - v;

			uint32_t ia = tris[tIdx * 3 + 0];
			uint32_t ib = tris[tIdx * 3 + 1];
			uint32_t ic = tris[tIdx * 3 + 2];

			// Bounds check for indices
			if (ia >= worldPositions.size() || ib >= worldPositions.size() || ic >= worldPositions.size()) {
				Warning("AddRigidParticleBody: Invalid index found during random sampling (%u, %u, %u vs %zu) for entity '%s'.", ia, ib, ic, worldPositions.size(), apSubMeshEntity->GetName().c_str());
				continue;
			}

			Vec3 pt = worldPositions[ia] * u + worldPositions[ib] * v + worldPositions[ic] * w;
			initialSamples.push_back(pt);
		}

		// 3. Add Voxelization Sampling if needed (afShellOnly == 0.0f)
		if (afShellOnly == 0.0f)
		{
			// Calculate world bounds of the mesh
			Vec3 meshMinExtents(FLT_MAX);
			Vec3 meshMaxExtents(-FLT_MAX);
			for (const auto& p : worldPositions) {
				meshMinExtents = Min(meshMinExtents, p);
				meshMaxExtents = Max(meshMaxExtents, p);
			}

			// Determine voxel grid resolution (example calculation, adjust as needed)
			Vec3 edges = meshMaxExtents - meshMinExtents;
			float spacing = afParticleRadius; // Use particle radius as voxel spacing
			float spacingEps = spacing * (1.0f - 1e-4f);
			int dx = (spacing > edges.x || spacingEps <= 0.0f) ? 1 : int(edges.x / spacingEps);
			int dy = (spacing > edges.y || spacingEps <= 0.0f) ? 1 : int(edges.y / spacingEps);
			int dz = (spacing > edges.z || spacingEps <= 0.0f) ? 1 : int(edges.z / spacingEps);

			// Clamp dimensions to avoid excessive memory allocation
			const int maxDim = 128; // Example limit
			dx = Min(dx, maxDim);
			dy = Min(dy, maxDim);
			dz = Min(dz, maxDim);

			if (dx <= 0 || dy <= 0 || dz <= 0) {
				Warning("AddRigidParticleBody: Invalid voxel dimensions calculated (%d, %d, %d) for entity '%s'. Skipping voxelization.", dx, dy, dz, apSubMeshEntity->GetName().c_str());
			}
			else {
				Log("AddRigidParticleBody: Voxelizing entity '%s' with grid %d x %d x %d.", apSubMeshEntity->GetName().c_str(), dx, dy, dz);

				std::vector<uint32_t> voxels(dx * dy * dz);
				Vec3 voxelBase = meshMinExtents - Vec3(spacing * 0.5f); // Center voxels
				Vec3 voxelMax = voxelBase + Vec3(dx * spacing, dy * spacing, dz * spacing);

				// Prepare vertex data for Voxelize function (requires float array)
				std::vector<float> voxelVerts(vCount * 3);
				for (size_t i = 0; i < vCount; ++i) {
					voxelVerts[i * 3 + 0] = worldPositions[i].x;
					voxelVerts[i * 3 + 1] = worldPositions[i].y;
					voxelVerts[i * 3 + 2] = worldPositions[i].z;
				}

				// Voxelize the mesh
				Voxelize(&voxelVerts[0], vCount, (const int*)tris.data(), tris.size(), dx, dy, dz, &voxels[0], voxelBase, voxelMax);

				// Add samples from occupied voxels
				for (int z = 0; z < dz; ++z) {
					for (int y = 0; y < dy; ++y) {
						for (int x = 0; x < dx; ++x) {
							const int index = z * dx * dy + y * dx + x;
							if (voxels[index]) {
								Vec3 position = voxelBase + spacing * Vec3(float(x) + 0.5f, float(y) + 0.5f, float(z) + 0.5f);
								initialSamples.push_back(position);
							}
						}
					}
				}
			}
		}


		if (initialSamples.empty()) {
			Warning("AddRigidParticleBody: No initial samples generated (vertices/random/voxel) for entity '%s'.", apSubMeshEntity->GetName().c_str());
			return;
		}

		// --- Clustering ---
		std::vector<int> clusterIndices;
		std::vector<int> clusterOffsets;
		std::vector<Vec3> finalSamplePos; // Output: Clustered particle positions (world space)
		std::vector<float> priority(initialSamples.size(), 1.0f); // Dummy priority

		// Cluster the initial samples based on the particle radius
		CreateClusters(&initialSamples[0], &priority[0], initialSamples.size(), clusterOffsets, clusterIndices, finalSamplePos, afParticleRadius);

		Log("AddRigidParticleBody: Clustered %zu initial samples into %zu final particles for entity '%s'.", initialSamples.size(), finalSamplePos.size(), apSubMeshEntity->GetName().c_str());

		if (finalSamplePos.empty()) {
			Warning("AddRigidParticleBody: CreateClusters returned no particles for entity '%s'.", apSubMeshEntity->GetName().c_str());
			return;
		}

		// --- Append Clustered Particles to Flex Buffers ---
		MapBuffers(apSimBuffers); // Map ONCE

		const int base = apSimBuffers->positions.size();
		const int phase = NvFlexMakePhase(1, 0);
		const int particleCount = int(finalSamplePos.size());

		if (particleCount <= 0) {
			Error("AddRigidParticleBody: particleCount is zero or negative after clustering!");
			UnmapBuffers(apSimBuffers);
			return;
		}

		// Resize Flex buffers ONCE
		apSimBuffers->positions.resize(base + particleCount);
		apSimBuffers->velocities.resize(base + particleCount);
		apSimBuffers->phases.resize(base + particleCount);
		apSimBuffers->activeIndices.resize(base + particleCount);

		// Populate Flex buffers
		int currentIdx = base;
		for (const Vec3& p : finalSamplePos) {
			apSimBuffers->positions[currentIdx] = Vec4(p, 1.0f);
			apSimBuffers->velocities[currentIdx] = Vec3(0.0f);
			apSimBuffers->phases[currentIdx] = phase;
			apSimBuffers->activeIndices[currentIdx] = currentIdx;
			currentIdx++;
		}

		// --- Build Rigid Body Definition ---
		NvFlexVector<int>  rigidIdx(apFlexLibrary, particleCount);
		NvFlexVector<int>  rigidOff(apFlexLibrary, 2);
		NvFlexVector<float> rigidCoef(apFlexLibrary, 1);

		rigidIdx.map(); if (!rigidIdx.mappedPtr) { FatalError("Failed to map rigidIdx buffer!"); UnmapBuffers(apSimBuffers); return; }
		for (int i = 0; i < particleCount; ++i) rigidIdx[i] = base + i;
		rigidIdx.unmap();

		rigidOff.map(); if (!rigidOff.mappedPtr) { FatalError("Failed to map rigidOff buffer!"); UnmapBuffers(apSimBuffers); return; }
		rigidOff[0] = 0;
		rigidOff[1] = particleCount;
		rigidOff.unmap();

		rigidCoef.map(); if (!rigidCoef.mappedPtr) { FatalError("Failed to map rigidCoef buffer!"); UnmapBuffers(apSimBuffers); return; }
		rigidCoef[0] = afRigidStiffness;
		rigidCoef.unmap();

		// --- Set Up Rest Positions and Normals for Flex ---
		NvFlexVector<Vec3> restPos(apFlexLibrary, particleCount);
		NvFlexVector<Vec4> restNrm(apFlexLibrary, particleCount);

		restPos.map(); if (!restPos.mappedPtr) { FatalError("Failed to map restPos buffer!"); UnmapBuffers(apSimBuffers); return; }
		restNrm.map(); if (!restNrm.mappedPtr) { FatalError("Failed to map restNrm buffer!"); UnmapBuffers(apSimBuffers); return; }

		for (int i = 0; i < particleCount; ++i) {
			restPos[i] = Vec3(apSimBuffers->positions[base + i]);
			restNrm[i] = Vec4(0.0f, 0.0f, 1.0f, 0.0f);
		}

		restPos.unmap();
		restNrm.unmap();

		UnmapBuffers(apSimBuffers); // Unmap ONCE

		// --- Send Data to Flex Solver ---
		int totalParticles = apSimBuffers->positions.size();

		NvFlexSetParticles(apFlexSolver, apSimBuffers->positions.buffer, totalParticles);
		NvFlexSetVelocities(apFlexSolver, apSimBuffers->velocities.buffer, totalParticles);
		NvFlexSetPhases(apFlexSolver, apSimBuffers->phases.buffer, totalParticles);
		NvFlexSetActive(apFlexSolver, apSimBuffers->activeIndices.buffer, totalParticles);

		NvFlexSetRigids(apFlexSolver,
			rigidOff.buffer,
			rigidIdx.buffer,
			restPos.buffer,
			restNrm.buffer,
			rigidCoef.buffer,
			nullptr,
			nullptr,
			1,
			particleCount);

		Log("AddRigidParticleBody: Successfully added rigid body for entity '%s' with %d particles.", apSubMeshEntity->GetName().c_str(), particleCount);
	}

	SimBuffers* cFlexHelper::AllocBuffers(NvFlexLibrary* apFlexLibrary)
	{
		return new SimBuffers(apFlexLibrary);
	}

	void cFlexHelper::MapBuffers(SimBuffers* apSimBuffers)
	{
		apSimBuffers->positions.map();
		apSimBuffers->velocities.map();
		apSimBuffers->phases.map();
		apSimBuffers->activeIndices.map();
	}

	void cFlexHelper::UnmapBuffers(SimBuffers* apSimBuffers)
	{
		apSimBuffers->positions.unmap();
		apSimBuffers->velocities.unmap();
		apSimBuffers->phases.unmap();
		apSimBuffers->activeIndices.unmap();
	}

	void cFlexHelper::DestroyBuffers(SimBuffers* apSimBuffers)
	{
		apSimBuffers->positions.destroy();
		apSimBuffers->velocities.destroy();
		apSimBuffers->phases.destroy();
		apSimBuffers->activeIndices.destroy();

		delete apSimBuffers;
	}

	void cFlexHelper::CreateCollisionPlanes(NvFlexParams& params, cVector3f avMinX, cVector3f avMaxX)
	{
		params.planes[0][0] = 1.0f; params.planes[0][1] = 0.0f; params.planes[0][2] = 0.0f; params.planes[0][3] = -avMinX.x;
		params.planes[1][0] = -1.0f; params.planes[1][1] = 0.0f; params.planes[1][2] = 0.0f; params.planes[1][3] = avMaxX.x;
		params.planes[2][0] = 0.0f; params.planes[2][1] = 1.0f; params.planes[2][2] = 0.0f; params.planes[2][3] = -avMinX.y;
		params.planes[3][0] = 0.0f; params.planes[3][1] = -1.0f; params.planes[3][2] = 0.0f; params.planes[3][3] = avMaxX.y;
		params.planes[4][0] = 0.0f; params.planes[4][1] = 0.0f; params.planes[4][2] = 1.0f; params.planes[4][3] = -avMinX.z;
		params.planes[5][0] = 0.0f; params.planes[5][1] = 0.0f; params.planes[5][2] = -1.0f; params.planes[5][3] = avMaxX.z;
		params.numPlanes = 6;
	}

	void cFlexHelper::DrawCollisionPlanes(NvFlexParams& params, const cColor& aSolidColor, const cColor& aWireColor, bool abDrawFloor)
	{
		const float fMinX = -params.planes[0][3];
		const float fMaxX = params.planes[1][3];
		const float fMinY = -params.planes[2][3];
		const float fMaxY = params.planes[3][3];
		const float fMinZ = -params.planes[4][3];
		const float fMaxZ = params.planes[5][3];

		const cVector3f v000{ fMinX, fMinY, fMinZ };
		const cVector3f v001{ fMinX, fMinY, fMaxZ };
		const cVector3f v010{ fMinX, fMaxY, fMinZ };
		const cVector3f v011{ fMinX, fMaxY, fMaxZ };
		const cVector3f v100{ fMaxX, fMinY, fMinZ };
		const cVector3f v101{ fMaxX, fMinY, fMaxZ };
		const cVector3f v110{ fMaxX, fMaxY, fMinZ };
		const cVector3f v111{ fMaxX, fMaxY, fMaxZ };

		static const std::array<std::array<cVector3f, 3>, 12> tris{{
			{{ v000, v001, v011 }}, {{ v000, v011, v010 }},
			{{ v100, v110, v101 }}, {{ v110, v111, v101 }},
			{{ v000, v010, v100 }}, {{ v010, v110, v100 }},
			{{ v001, v111, v011 }}, {{ v001, v101, v111 }},
			{{ v000, v100, v001 }}, {{ v100, v101, v001 }},
			{{ v010, v011, v111 }}, {{ v010, v111, v110 }}
		}};

		// Solid
		glDisable(GL_CULL_FACE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glColor4f(aSolidColor.r, aSolidColor.g, aSolidColor.b, aSolidColor.a);

		glBegin(GL_TRIANGLES);
		for (size_t i = 0; i < tris.size(); ++i)
		{
			if (!abDrawFloor && (i == 8 || i == 9))
				continue;

			const auto& t = tris[i];
			glVertex3f(t[0].x, t[0].y, t[0].z);
			glVertex3f(t[1].x, t[1].y, t[1].z);
			glVertex3f(t[2].x, t[2].y, t[2].z);
		}
		glEnd();

		// Wire
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glLineWidth(1.0f);
		glColor4f(aWireColor.r, aWireColor.g, aWireColor.b, aWireColor.a);

		glBegin(GL_TRIANGLES);
		for (size_t i = 0; i < tris.size(); ++i)
		{
			if (!abDrawFloor && (i == 8 || i == 9))
				continue;

			const auto& t = tris[i];
			glVertex3f(t[0].x, t[0].y, t[0].z);
			glVertex3f(t[1].x, t[1].y, t[1].z);
			glVertex3f(t[2].x, t[2].y, t[2].z);
		}
		glEnd();

		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glLineWidth(1.0f);
		glEnable(GL_CULL_FACE);
	}

	void cFlexHelper::SampleMesh(Mesh* mesh, Vec3 lower, Vec3 scale, float rotation, float radius, float volumeSampling, float surfaceSampling, std::vector<Vec3>& outPositions)
	{
		if (!mesh)
			return;

		mesh->Transform(RotationMatrix(rotation, Vec3(0.0f, 1.0f, 0.0f)));

		Vec3 meshLower, meshUpper;
		mesh->GetBounds(meshLower, meshUpper);

		Vec3 edges = meshUpper - meshLower;
		float maxEdge = Max(Max(edges.x, edges.y), edges.z);

		// put mesh at the origin and scale to specified size
		Matrix44 xform = ScaleMatrix(scale / maxEdge) * TranslationMatrix(Point3(-meshLower));

		mesh->Transform(xform);
		mesh->GetBounds(meshLower, meshUpper);

		std::vector<Vec3> samples;

		if (volumeSampling > 0.0f)
		{
			// recompute expanded edges
			edges = meshUpper - meshLower;
			maxEdge = Max(Max(edges.x, edges.y), edges.z);

			// use a higher resolution voxelization as a basis for the particle decomposition
			float spacing = radius / volumeSampling;

			// tweak spacing to avoid edge cases for particles laying on the boundary
			// just covers the case where an edge is a whole multiple of the spacing.
			float spacingEps = spacing * (1.0f - 1e-4f);

			// make sure to have at least one particle in each dimension
			int dx, dy, dz;
			dx = spacing > edges.x ? 1 : int(edges.x / spacingEps);
			dy = spacing > edges.y ? 1 : int(edges.y / spacingEps);
			dz = spacing > edges.z ? 1 : int(edges.z / spacingEps);

			int maxDim = Max(Max(dx, dy), dz);

			// expand border by two voxels to ensure adequate sampling at edges
			meshLower -= 2.0f * Vec3(spacing);
			meshUpper += 2.0f * Vec3(spacing);
			maxDim += 4;

			std::vector<uint32_t> voxels(maxDim * maxDim * maxDim);

			// we shift the voxelization bounds so that the voxel centers
			// lie symmetrically to the center of the object. this reduces the 
			// chance of missing features, and also better aligns the particles
			// with the mesh
			Vec3 meshOffset;
			meshOffset.x = 0.5f * (spacing - (edges.x - (dx - 1) * spacing));
			meshOffset.y = 0.5f * (spacing - (edges.y - (dy - 1) * spacing));
			meshOffset.z = 0.5f * (spacing - (edges.z - (dz - 1) * spacing));
			meshLower -= meshOffset;

			//Voxelize(*mesh, dx, dy, dz, &voxels[0], meshLower - Vec3(spacing*0.05f) , meshLower + Vec3(maxDim*spacing) + Vec3(spacing*0.05f));
			Voxelize((const float*)&mesh->m_positions[0], mesh->m_positions.size(), (const int*)&mesh->m_indices[0], mesh->m_indices.size(), maxDim, maxDim, maxDim, &voxels[0], meshLower, meshLower + Vec3(maxDim * spacing));

			// sample interior
			for (int x = 0; x < maxDim; ++x)
			{
				for (int y = 0; y < maxDim; ++y)
				{
					for (int z = 0; z < maxDim; ++z)
					{
						const int index = z * maxDim * maxDim + y * maxDim + x;

						// if voxel is marked as occupied the add a particle
						if (voxels[index])
						{
							Vec3 position = lower + meshLower + spacing * Vec3(float(x) + 0.5f, float(y) + 0.5f, float(z) + 0.5f);

							// normalize the sdf value and transform to world scale
							samples.push_back(position);
						}
					}
				}
			}
		}

		// move back
		mesh->Transform(ScaleMatrix(1.0f) * TranslationMatrix(Point3(-0.5f * (meshUpper + meshLower))));
		mesh->Transform(TranslationMatrix(Point3(lower + 0.5f * (meshUpper + meshLower))));

		if (surfaceSampling > 0.0f)
		{
			// sample vertices
			for (int i = 0; i < int(mesh->m_positions.size()); ++i)
				samples.push_back(Vec3(mesh->m_positions[i]));

			// random surface sampling
			if (1)
			{
				for (int i = 0; i < 50000; ++i)
				{
					int t = Rand() % mesh->GetNumFaces();
					float u = Randf();
					float v = Randf() * (1.0f - u);
					float w = 1.0f - u - v;

					int a = mesh->m_indices[t * 3 + 0];
					int b = mesh->m_indices[t * 3 + 1];
					int c = mesh->m_indices[t * 3 + 2];

					Point3 pt = mesh->m_positions[a] * u + mesh->m_positions[b] * v + mesh->m_positions[c] * w;
					Vec3 p(pt.x, pt.y, pt.z);

					samples.push_back(p);
				}
			}
		}

		std::vector<int> clusterIndices;
		std::vector<int> clusterOffsets;
		std::vector<Vec3> clusterPositions;
		std::vector<float> priority(samples.size());

		CreateClusters(&samples[0], &priority[0], samples.size(), clusterOffsets, clusterIndices, outPositions, radius);

	}

	int cFlexHelper::CreateClusters(Vec3* particles, const float* priority, int numParticles, std::vector<int>& outClusterOffsets, std::vector<int>& outClusterIndices, std::vector<Vec3>& outClusterPositions, float radius, float smoothing)
	{
		std::vector<Seed> seeds;
		std::vector<Cluster> clusters;

		// flags a particle as belonging to at least one cluster
		std::vector<bool> used(numParticles, false);

		// initialize seeds
		for (int i = 0; i < numParticles; ++i)
		{
			Seed s;
			s.index = i;
			s.priority = priority[i];

			seeds.push_back(s);
		}

		std::stable_sort(seeds.begin(), seeds.end());

		while (seeds.size())
		{
			// pick highest unused particle from the seeds list
			Seed seed = seeds.back();
			seeds.pop_back();

			if (!used[seed.index])
			{
				Cluster c;

				const float radiusSq = Sqr(radius);

				// push all neighbors within radius
				for (int p = 0; p < numParticles; ++p)
				{
					float dSq = LengthSq(Vec3(particles[seed.index]) - Vec3(particles[p]));
					if (dSq <= radiusSq)
					{
						c.indices.push_back(p);

						used[p] = true;
					}
				}

				c.mean = CalculateMean(particles, &c.indices[0], c.indices.size());

				clusters.push_back(c);
			}
		}

		if (smoothing > 0.0f)
		{
			// expand clusters by smoothing radius
			float radiusSmoothSq = Sqr(smoothing);

			for (int i = 0; i < int(clusters.size()); ++i)
			{
				Cluster& c = clusters[i];

				// clear cluster indices
				c.indices.resize(0);

				// push all neighbors within radius
				for (int p = 0; p < numParticles; ++p)
				{
					float dSq = LengthSq(c.mean - Vec3(particles[p]));
					if (dSq <= radiusSmoothSq)
						c.indices.push_back(p);
				}

				c.mean = CalculateMean(particles, &c.indices[0], c.indices.size());
			}
		}

		// write out cluster indices
		int count = 0;

		//outClusterOffsets.push_back(0);

		for (int c = 0; c < int(clusters.size()); ++c)
		{
			const Cluster& cluster = clusters[c];

			const int clusterSize = int(cluster.indices.size());

			// skip empty clusters
			if (clusterSize)
			{
				// write cluster indices
				for (int i = 0; i < clusterSize; ++i)
					outClusterIndices.push_back(cluster.indices[i]);

				// write cluster offset
				outClusterOffsets.push_back(outClusterIndices.size());

				// write center
				outClusterPositions.push_back(cluster.mean);

				++count;
			}
		}

		return count;
	}

	Vec3 cFlexHelper::CalculateMean(const Vec3* particles, const int* indices, int numIndices)
	{
		Vec3 sum;

		for (int i = 0; i < numIndices; ++i)
			sum += Vec3(particles[indices[i]]);

		if (numIndices)
			return sum / float(numIndices);
		else
			return sum;
	}
}