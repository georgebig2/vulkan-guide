#include "vk_types.h"
#include <vk_scene.h>
#include <rengine.h>
#include "Tracy.hpp"
#include "logger.h"
#include "cvars.h"


void RenderScene::init()
{
	_forwardPass.type = MeshpassType::Forward;
	_shadowPass.type = MeshpassType::DirectionalShadow;
	_transparentForwardPass.type = MeshpassType::Transparency;
}

Handle<RenderObject> RenderScene::register_object(MeshObject* object)
{
	RenderObject newObj;
	newObj.bounds = object->bounds;
	newObj.transformMatrix = object->transformMatrix;	
	newObj.material = getMaterialHandle(object->material);
	newObj.meshID = getMeshHandle(object->mesh);
	newObj.updateIndex = (uint32_t)-1;
	newObj.customSortKey = object->customSortKey;
	newObj.passIndices.clear(-1);
	Handle<RenderObject> handle;
	handle.handle = static_cast<uint32_t>(renderables.size());
	
	renderables.push_back(newObj);

	if (object->bDrawForwardPass)
	{
		if (object->material && object->material->original->passShaders[MeshpassType::Transparency])
		{
			_transparentForwardPass.unbatchedObjects.push_back(handle);
		}
		if (object->material && object->material->original->passShaders[MeshpassType::Forward])
		{
			_forwardPass.unbatchedObjects.push_back(handle);
		}
	}
	if (object->bDrawShadowPass)
	{
		if (object->material && object->material->original->passShaders[MeshpassType::DirectionalShadow])
		{
			_shadowPass.unbatchedObjects.push_back(handle);
		}
	}

	update_object(handle);
	return handle;
}

void RenderScene::register_object_batch(MeshObject* first, uint32_t count)
{
	renderables.reserve(count);

	for (uint32_t i = 0; i < count; i++) {
		register_object(&(first[i]));
	}
}

void RenderScene::update_transform(Handle<RenderObject> objectID, const glm::mat4& localToWorld)
{
	get_object(objectID)->transformMatrix = localToWorld;
	update_object(objectID);
}


void RenderScene::update_object(Handle<RenderObject> objectID)
{
	auto& passIndices = get_object(objectID)->passIndices;
	if (passIndices[MeshpassType::Forward] != -1)
	{
		Handle<PassObject> obj;
		obj.handle = passIndices[MeshpassType::Forward];

		_forwardPass.objectsToDelete.push_back(obj);
		_forwardPass.unbatchedObjects.push_back(objectID);

		passIndices[MeshpassType::Forward] = -1;
	}


	if (passIndices[MeshpassType::DirectionalShadow] != -1)
	{
		Handle<PassObject> obj;
		obj.handle = passIndices[MeshpassType::DirectionalShadow];

		_shadowPass.objectsToDelete.push_back(obj);
		_shadowPass.unbatchedObjects.push_back(objectID);

		passIndices[MeshpassType::DirectionalShadow] = -1;
	}


	if (passIndices[MeshpassType::Transparency] != -1)
	{
		Handle<PassObject> obj;
		obj.handle = passIndices[MeshpassType::Transparency];

		_transparentForwardPass.unbatchedObjects.push_back(objectID);
		_transparentForwardPass.objectsToDelete.push_back(obj);

		passIndices[MeshpassType::Transparency] = -1;
	}

	
	if (get_object(objectID)->updateIndex == (uint32_t)-1)
	{
		get_object(objectID)->updateIndex = static_cast<uint32_t>(dirtyObjects.size());
		dirtyObjects.push_back(objectID);
	}
}

void RenderScene::write_object(GPUObjectData* target, Handle<RenderObject> objectID)
{
	RenderObject* renderable = get_object(objectID);
	GPUObjectData object;

	object.modelMatrix = renderable->transformMatrix;
	object.origin_rad = glm::vec4(renderable->bounds.origin, renderable->bounds.radius);
	//object.extents = glm::vec4(renderable->bounds.extents, renderable->bounds.valid ? 1.f : 0.f);

	memcpy(target, &object, sizeof(GPUObjectData));
}

void RenderScene::fill_objectData(GPUObjectData* data)
{
	
	for(int i = 0; i < renderables.size(); i++)
	{
		Handle<RenderObject> h;
		h.handle = i;
		write_object(data + i, h);
	}
}


void RenderScene::fill_indirectArray(GPUIndirectObject* data, MeshPass& pass)
{
	ZoneScopedNC("Fill Indirect", tracy::Color::Red);
	int dataIndex = 0;
	for (int i = 0; i < pass.batches.size(); i++) {

		auto batch = pass.batches[i];

		data[dataIndex].command.firstInstance = batch.first;//i;
		data[dataIndex].command.instanceCount = 0;
		data[dataIndex].command.firstIndex = get_mesh(batch.meshID)->firstIndex;
		data[dataIndex].command.vertexOffset = get_mesh(batch.meshID)->firstVertex;
		data[dataIndex].command.indexCount = get_mesh(batch.meshID)->indexCount;
		data[dataIndex].objectID = 0;
		data[dataIndex].batchID = i;

		dataIndex++;
	}
}

void RenderScene::fill_instancesArray(GPUInstance* data, MeshPass& pass)
{
	ZoneScopedNC("Fill Instances", tracy::Color::Red);
	int dataIndex = 0;
	for (int i = 0; i < pass.batches.size(); i++) {

		auto& batch = pass.batches[i];
		for (int b = 0; b < batch.count; b++)
		{
			data[dataIndex].objectID = pass.get(pass.flat_batches[b + batch.first].object)->original.handle;
			data[dataIndex].batchID = i;
			dataIndex++;
		}
	}
}

void RenderScene::clear_dirty_objects()
{
	for (auto obj : dirtyObjects)
	{
		get_object(obj)->updateIndex = (uint32_t)-1;
	}
	dirtyObjects.clear();
}
#include <future>
void RenderScene::build_batches()
{
#if 1
	auto fwd = std::async(std::launch::async, [&] { refresh_pass(&_forwardPass); });
	auto shadow = std::async(std::launch::async, [&] { refresh_pass(&_shadowPass); });
	auto transparent = std::async(std::launch::async, [&] { refresh_pass(&_transparentForwardPass); });

	transparent.get();
	shadow.get();
	fwd.get();
#else
	refresh_pass(&_forwardPass);
	refresh_pass(&_transparentForwardPass);
	refresh_pass(&_shadowPass);
#endif
	
}

AllocatedBufferUntyped create_buffer(REngine*, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags required_flags = 0);

void RenderScene::merge_meshes(REngine* engine)
{
	ZoneScopedNC("Mesh Merge", tracy::Color::Magenta);

	size_t total_vertices = 0;
	size_t total_indices = 0;
	for (auto& m : meshes)
	{
		m.firstIndex = static_cast<uint32_t>(total_indices);
		m.firstVertex = static_cast<uint32_t>(total_vertices);
		total_vertices += m.vertexCount;
		total_indices += m.indexCount;
		m.isMerged = true;
	}

	mergedVertexBufferP = create_buffer(engine, total_vertices * sizeof(VertexP), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);
	mergedVertexBufferA = create_buffer(engine, total_vertices * sizeof(VertexA), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

	mergedIndexBuffer = create_buffer(engine, total_indices * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

	engine->_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(engine->_allocator, mergedIndexBuffer._buffer, mergedIndexBuffer._allocation);
		vmaDestroyBuffer(engine->_allocator, mergedVertexBufferA._buffer, mergedVertexBufferA._allocation);
		vmaDestroyBuffer(engine->_allocator, mergedVertexBufferP._buffer, mergedVertexBufferP._allocation);
		});

	engine->immediate_submit([&](VkCommandBuffer cmd)
	{
		for (auto& m : meshes)
		{
			{
				VkBufferCopy vertexCopy;
				vertexCopy.dstOffset = m.firstVertex * sizeof(VertexP);
				vertexCopy.size = m.vertexCount * sizeof(VertexP);
				vertexCopy.srcOffset = 0;
				vkCmdCopyBuffer(cmd, m.original->_vertexBuffer_p._buffer, mergedVertexBufferP._buffer, 1, &vertexCopy);
			}
			{
				VkBufferCopy vertexCopy;
				vertexCopy.dstOffset = m.firstVertex * sizeof(VertexA);
				vertexCopy.size = m.vertexCount * sizeof(VertexA);
				vertexCopy.srcOffset = 0;
				vkCmdCopyBuffer(cmd, m.original->_vertexBuffer_a._buffer, mergedVertexBufferA._buffer, 1, &vertexCopy);
			}

			VkBufferCopy indexCopy;
			indexCopy.dstOffset = m.firstIndex * sizeof(uint32_t);
			indexCopy.size = m.indexCount * sizeof(uint32_t);
			indexCopy.srcOffset = 0;
			vkCmdCopyBuffer(cmd, m.original->_indexBuffer._buffer, mergedIndexBuffer._buffer, 1, &indexCopy);
		}
	});
}

AutoCVar_Int CVAR_Batch("gpu.batch", "Batch objects", 1, CVarFlags::EditCheckbox);


void RenderScene::refresh_pass(MeshPass* pass)
{
	pass->needsIndirectRefresh = true;
	pass->needsInstanceRefresh = true;

	std::vector<uint32_t> new_objects;
	if(pass->objectsToDelete.size() > 0)
	{
		ZoneScopedNC("Delete objects", tracy::Color::Blue3);

		//create the render batches so that then we can do the deletion on the flat-array directly

		std::vector<RenderBatch> deletion_batches;
		deletion_batches.reserve(new_objects.size());
		
	
		for (auto i : pass->objectsToDelete) {
			pass->reusableObjects.push_back(i);
			RenderBatch newCommand;

			auto obj = pass->objects[i.handle];
			newCommand.object= i;

			uint64_t pipelinehash = std::hash<uint64_t>()(uint64_t(obj.material.shaderPass->pipeline));
			uint64_t sethash = std::hash<uint64_t>()((uint64_t)obj.material.materialSet);

			uint32_t mathash = static_cast<uint32_t>(pipelinehash ^ sethash);

			uint32_t meshmat = uint64_t(mathash) ^ uint64_t(obj.meshID.handle);

			//pack mesh id and material into 64 bits				
			newCommand.sortKey = uint64_t(meshmat) | (uint64_t(obj.customKey) << 32);

			pass->objects[i.handle].customKey = 0;
			pass->objects[i.handle].material.shaderPass = nullptr;
			pass->objects[i.handle].meshID.handle = -1;
			pass->objects[i.handle].original.handle = -1;

			deletion_batches.push_back(newCommand);
			
		}
		pass->objectsToDelete.clear();
		{
			ZoneScopedNC("Deletion Sort", tracy::Color::Blue1);
			std::sort(deletion_batches.begin(), deletion_batches.end(), [](const RenderBatch& A, const RenderBatch& B) {
				if (A.sortKey < B.sortKey) { return true; }
				else if (A.sortKey == B.sortKey) { return A.object.handle < B.object.handle; }
				else { return false; }
			});
		}
		{
			ZoneScopedNC("removal", tracy::Color::Blue1);

			std::vector<RenderBatch> newbatches;
			newbatches.reserve(pass->flat_batches.size());

			{
				ZoneScopedNC("Set Difference", tracy::Color::Red);

				std::set_difference(pass->flat_batches.begin(), pass->flat_batches.end(), deletion_batches.begin(), deletion_batches.end(), std::back_inserter(newbatches), [](const RenderBatch& A, const RenderBatch& B) {
					if (A.sortKey < B.sortKey) { return true; }
					else if (A.sortKey == B.sortKey) { return A.object.handle < B.object.handle; }
					else { return false; }
				});
			}
			pass->flat_batches = std::move(newbatches);
		}
	}
	{
		ZoneScopedNC("Fill ObjectList", tracy::Color::Blue2);
			
		new_objects.reserve(pass->unbatchedObjects.size());
		for (auto o : pass->unbatchedObjects)
		{
			PassObject newObject;

			newObject.original = o;
			newObject.meshID = get_object(o)->meshID;

			//pack mesh id and material into 32 bits
			vkutil::Material* mt = get_material(get_object(o)->material);
			newObject.material.materialSet = mt->passSets[pass->type];
			newObject.material.shaderPass = mt->original->passShaders[pass->type];
			newObject.customKey = get_object(o)->customSortKey;

			uint32_t handle = -1;

			//reuse handle
			if (pass->reusableObjects.size() > 0)
			{
				handle = pass->reusableObjects.back().handle;
				pass->reusableObjects.pop_back();
				pass->objects[handle] = newObject;
			}
			else 
			{
				handle = pass->objects.size();
				pass->objects.push_back(newObject);
			}

			
			new_objects.push_back(handle);
			get_object(o)->passIndices[pass->type] = static_cast<int32_t>(handle);
		}

		pass->unbatchedObjects.clear();
	}

	std::vector<RenderBatch> new_batches;
	new_batches.reserve(new_objects.size());

	{
		ZoneScopedNC("Fill DrawList", tracy::Color::Blue2);	
		
		for (auto i : new_objects) {
			{
				RenderBatch newCommand;

				auto obj = pass->objects[i];
				newCommand.object.handle = i;

				uint64_t pipelinehash = std::hash<uint64_t>()(uint64_t(obj.material.shaderPass->pipeline));
				uint64_t sethash = std::hash<uint64_t>()((uint64_t)obj.material.materialSet);

				uint32_t mathash = static_cast<uint32_t>(pipelinehash ^ sethash);
				
				uint32_t meshmat = uint64_t(mathash) ^ uint64_t(obj.meshID.handle);

				//pack mesh id and material into 64 bits				
				newCommand.sortKey = uint64_t(meshmat) | (uint64_t(obj.customKey) << 32);

				new_batches.push_back(newCommand);
			}
		}
	}

	{
		ZoneScopedNC("Draw Sort", tracy::Color::Blue1);
		std::sort(new_batches.begin(), new_batches.end(), [](const RenderBatch& A, const RenderBatch& B) {
			if (A.sortKey < B.sortKey) { return true; }
			else if (A.sortKey == B.sortKey) { return A.object.handle < B.object.handle; }
			else { return false; }
		});
	}
	{
		ZoneScopedNC("Draw Merge batches", tracy::Color::Blue2);

		//merge the new batches into the main batch array

		if (pass->flat_batches.size() > 0 && new_batches.size() > 0)
		{
			size_t index = pass->flat_batches.size();
			pass->flat_batches.reserve(pass->flat_batches.size() + new_batches.size());
			
			for (auto b : new_batches)
			{
				pass->flat_batches.push_back(b);
			}

			RenderBatch* begin = pass->flat_batches.data();
			RenderBatch* mid = begin + index;
			RenderBatch* end = begin + pass->flat_batches.size();
			//std::sort(pass->flat_batches.begin(), pass->flat_batches.end(), [](const RenderScene::RenderBatch& A, const RenderScene::RenderBatch& B) {
			//	return A.sortKey < B.sortKey;
			//	});
			std::inplace_merge(begin, mid, end, [](const RenderBatch& A, const RenderBatch& B) {
				if (A.sortKey < B.sortKey) { return true; }
				else if (A.sortKey == B.sortKey) { return A.object.handle < B.object.handle; }
				else { return false; }
			});
		}
		else if (pass->flat_batches.size() == 0)
		{
			pass->flat_batches = std::move(new_batches);
		}
	}
	
	{
		ZoneScopedNC("Draw Merge", tracy::Color::Blue);

		pass->batches.clear();

		build_indirect_batches(pass,pass->batches,pass->flat_batches);

		//flatten batches into multibatch
		Multibatch newbatch;
		pass->multibatches.clear();

		newbatch.count = 1;
		newbatch.first = 0;
	
		if (CVAR_Batch.Get() > 0)
		{
			for (int i = 1; i < pass->batches.size(); i++)
			{
				IndirectBatch* joinbatch = &pass->batches[newbatch.first];
				IndirectBatch* batch = &pass->batches[i];

				bool bCompatibleMesh = get_mesh(joinbatch->meshID)->isMerged;
				bool bSameMat = false;
				if (bCompatibleMesh && joinbatch->material == batch->material)//  joinbatch->material.materialSet == batch->material.materialSet &&
	//				joinbatch->material.shaderPass == batch->material.shaderPass)
				{
					bSameMat = true;
				}

				if (!bSameMat || !bCompatibleMesh)
				{
					pass->multibatches.push_back(newbatch);
					newbatch.count = 1;
					newbatch.first = i;
				}
				else {
					newbatch.count++;
				}
			}
			pass->multibatches.push_back(newbatch);
		}
		else {
			for (int i = 0; i < pass->batches.size(); i++)
			{
				Multibatch newbatch;
				newbatch.count = 1;
				newbatch.first = i;

				pass->multibatches.push_back(newbatch);
			}
		}
	}
}

void RenderScene::build_indirect_batches(MeshPass* pass, std::vector<IndirectBatch>& outbatches, const std::vector<RenderBatch>& inobjects)
{
	if (inobjects.size() == 0) return;

	ZoneScopedNC("Build Indirect Batches", tracy::Color::Blue);

	IndirectBatch newBatch;
	newBatch.first = 0;
	newBatch.count = 0;

	newBatch.material = pass->get(inobjects[0].object)->material;
	newBatch.meshID = pass->get(inobjects[0].object)->meshID;

	outbatches.push_back(newBatch);
	IndirectBatch* back = &pass->batches.back();

	PassMaterial lastMat = pass->get(inobjects[0].object)->material;
	for (int i = 0; i < inobjects.size(); i++)
	{
		PassObject* obj = pass->get(inobjects[i].object);

		bool bSameMesh = obj->meshID.handle == back->meshID.handle;
		bool bSameMaterial = false;
		if (obj->material == lastMat)
		{
			bSameMaterial = true;
		}
		if (!bSameMaterial || !bSameMesh)
		{
			newBatch.material = obj->material;
			if (newBatch.material == back->material)
			{
				bSameMaterial = true;
			}
		}

		if (bSameMesh && bSameMaterial)
		{
			back->count++;
		}
		else
		{
			newBatch.first = i;
			newBatch.count = 1;
			newBatch.meshID = obj->meshID;

			outbatches.push_back(newBatch);
			back = &outbatches.back();
		}
		//back->objects.push_back(obj->original);
	}
}

RenderObject* RenderScene::get_object(Handle<RenderObject> objectID)
{
	return &renderables[objectID.handle];
}

DrawMesh* RenderScene::get_mesh(Handle<DrawMesh> objectID)
{
	return &meshes[objectID.handle];
}

vkutil::Material* RenderScene::get_material(Handle<vkutil::Material> objectID)
{
	return materials[objectID.handle];
}

MeshPass* RenderScene::get_mesh_pass(MeshpassType name)
{
	switch (name)
	{	
	case MeshpassType::Forward:
		return &_forwardPass;
		break;
	case MeshpassType::Transparency:
		return &_transparentForwardPass;
		break;
	case MeshpassType::DirectionalShadow:
		return &_shadowPass;
		break;
	default:
		break;
	}
	return nullptr;
}

Handle<vkutil::Material> RenderScene::getMaterialHandle(vkutil::Material* m)
{	
	Handle<vkutil::Material> handle;
	auto it = materialConvert.find(m);
	if (it == materialConvert.end())
	{
		uint32_t index = static_cast<uint32_t>(materials.size());
		materials.push_back(m);

		handle.handle = index;
		materialConvert[m] = handle;
	}
	else {
		handle = (*it).second;
	}
	return handle;
}

Handle<DrawMesh> RenderScene::getMeshHandle(Mesh* m)
{
	Handle<DrawMesh> handle;
	auto it = meshConvert.find(m);
	if (it == meshConvert.end())
	{
		uint32_t index = static_cast<uint32_t>(meshes.size());

		DrawMesh newMesh;
		newMesh.original = m;
		newMesh.firstIndex = 0;
		newMesh.firstVertex = 0;
		newMesh.vertexCount = static_cast<uint32_t>(m->_vertices_p.size());
		newMesh.indexCount = static_cast<uint32_t>(m->_indices.size());

		meshes.push_back(newMesh);

		handle.handle = index;
		meshConvert[m] = handle;
	}
	else {
		handle = (*it).second;
	}
	return handle;
}

PassObject* MeshPass::get(Handle<PassObject> handle)
{
	return &objects[handle.handle];
}
