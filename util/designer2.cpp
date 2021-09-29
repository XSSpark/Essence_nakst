#define UI_IMPLEMENTATION
#define ES_CRT_WITHOUT_PREFIX
#define EsPainter _EsPainter
#define EsPaintTarget _EsPaintTarget
#include "luigi.h"
#undef EsPaintTarget
#undef EsPainter
#ifndef OS_ESSENCE
#include <stdio.h>
#include <math.h>
#include <limits.h>
#endif
#include "hsluv.h"

// x86_64-w64-mingw32-gcc -O3 -o bin/designer2.exe -D UI_WINDOWS util/designer2.cpp  -DUNICODE -lgdi32 -luser32 -lkernel32 -Wl,--subsystem,windows -fno-exceptions -fno-rtti

// Needed to replace the old designer:
// TODO Previewing state transitions.
// TODO Implement path layers, and test radial gradients with them.
// TODO Import and reorganize old theming data.
// TODO Export.

// Additional features:
// TODO Selecting and moving multiple objects.
// TODO Resizing objects?
// TODO Find object in graph by name.
// TODO Prototyping display. (Multiple instances of each object can be placed, resized and interacted with).
		
//////////////////////////////////////////////////////////////

#ifndef OS_ESSENCE

#define ES_TEXT_H_LEFT 	 (1 << 0)
#define ES_TEXT_H_CENTER (1 << 1)
#define ES_TEXT_H_RIGHT  (1 << 2)
#define ES_TEXT_V_TOP 	 (1 << 3)
#define ES_TEXT_V_CENTER (1 << 4)
#define ES_TEXT_V_BOTTOM (1 << 5)
#define ES_TEXT_ELLIPSIS (1 << 6)
#define ES_TEXT_WRAP 	 (1 << 7)

#define ES_FONT_SANS 	   (0xFFFF)
#define ES_FONT_SERIF 	   (0xFFFE)
#define ES_FONT_MONOSPACED (0xFFFD)

#define ES_FUNCTION_OPTIMISE_O2 __attribute__((optimize("-O2")))
#define ES_FUNCTION_OPTIMISE_O3 __attribute__((optimize("-O3")))

#define ES_INFINITY INFINITY

#define EsAssert assert
#define EsCRTacosf acosf
#define EsCRTatan2f atan2f
#define EsCRTceilf ceilf
#define EsCRTcosf cosf
#define EsCRTfabsf fabsf
#define EsCRTfloorf floorf
#define EsCRTfmodf fmodf
#define EsCRTisnanf isnan
#define EsCRTlog2f log2f
#define EsCRTsinf sinf
#define EsCRTsqrtf sqrtf
#define EsHeap void
#define EsMemoryCopy memcpy
#define EsMemoryCopyReverse memmove
#define EsMemoryZero(a, b) memset((a), 0, (b))
#define EsPanic(...) UI_ASSERT(false)
#define EsRectangle UIRectangle

#define ES_MEMORY_MOVE_BACKWARDS -

#define SHARED_COMMON_WANT_BUFFERS

struct EsBuffer {
	union { const uint8_t *in; uint8_t *out; };
	size_t position, bytes;
	bool error;
	void *context;
};

void *EsHeapAllocate(size_t bytes, bool zero, EsHeap *heap = nullptr) {
	(void) heap;
	return zero ? calloc(1, bytes) : malloc(bytes);
}

void EsHeapFree(void *pointer, size_t bytes = 0, EsHeap *heap = nullptr) {
	(void) heap;
	(void) bytes;
	free(pointer);
}

void *EsHeapReallocate(void *oldAddress, size_t newAllocationSize, bool zeroNewSpace, EsHeap *) {
	UI_ASSERT(!zeroNewSpace);
	return realloc(oldAddress, newAllocationSize);
}

void EsMemoryMove(void *_start, void *_end, intptr_t amount, bool zeroEmptySpace) {
	uint8_t *start = (uint8_t *) _start;
	uint8_t *end = (uint8_t *) _end;
	if (end < start) return;

	if (amount > 0) {
		EsMemoryCopyReverse(start + amount, start, end - start);
		if (zeroEmptySpace) EsMemoryZero(start, amount);
	} else if (amount < 0) {
		EsMemoryCopy(start + amount, start, end - start);
		if (zeroEmptySpace) EsMemoryZero(end + amount, -amount);
	}
}

bool EsColorIsLight(uint32_t color) {
	float r = (color & 0xFF0000) >> 16;
	float g = (color & 0x00FF00) >>  8;
	float b = (color & 0x0000FF) >>  0;
	float brightness = r * r * 0.241f + g * g * 0.691f + b * b * 0.068f;
	return brightness >= 180.0f * 180.0f;
}

#endif

struct EsPaintTarget {
	void *bits;
	uint32_t width, height, stride;
	bool fullAlpha, readOnly;
};

struct EsPainter {
	EsRectangle clip;
	EsPaintTarget *target;
};

#define IN_DESIGNER
#define DESIGNER2

#define SHARED_COMMON_WANT_RECTANGLES
#define SHARED_COMMON_WANT_RENDERING
#include "../shared/common.cpp"
#define SHARED_MATH_WANT_BASIC_UTILITIES
#define SHARED_MATH_WANT_INTERPOLATION
#include "../shared/math.cpp"

#include "../shared/array.cpp"
#include "../desktop/renderer.cpp"
#include "../desktop/theme.cpp"

//////////////////////////////////////////////////////////////

const char *cPrimaryStateStrings[] = {
	"Any", "Idle", "Hovered", "Pressed", "Disabled", "Inactive",
};

const char *cStateBitStrings[] = {
	"Focus", "Check", "Indtm", "DefBtn", "Sel", "FcItem", "ListFc", "BfEnt", "AfExt",
};

const char *cCursorStrings[] = {
	"Normal", "Text",
	"Resize vertical", "Resize horizontal",
	"Diagonal 1", "Diagonal 2",
	"Split vertical", "Split horizontal",
	"Hand hover", "Hand drag", "Hand point",
	"Scroll up-left", "Scroll up", "Scroll up-right", "Scroll left",
	"Scroll center", "Scroll right", "Scroll down-left", "Scroll down", "Scroll down-right",
	"Select lines", "Drop text",
	"Cross hair pick", "Cross hair resize",
	"Move hover", "Move drag",
	"Rotate hover", "Rotate drag",
	"Blank",
};

//////////////////////////////////////////////////////////////

#ifdef OS_ESSENCE
EsFileStore *fileStore;

const EsInstanceClassEditorSettings instanceClassEditorSettings = {
	"untitled.designer", -1,
	"New design", -1,
	ES_ICON_TEXT_CSS,
};
#endif

UIWindow *window;
UIElement *canvas;
UIElement *inspector;
UIElement *canvasControls;
UILabel *labelMessage;

uint64_t selectedObjectID;

void InspectorAnnouncePropertyChanged(uint64_t objectID, const char *cPropertyName);
void InspectorPopulate();
void InspectorPickTargetEnd();
void CanvasSelectObject(struct Object *object);

//////////////////////////////////////////////////////////////

enum PropertyType : uint8_t {
	PROP_NONE,
	PROP_COLOR,
	PROP_INT,
	PROP_OBJECT,
	PROP_FLOAT,
};

struct Property {
	PropertyType type;
#define PROPERTY_NAME_SIZE (31)
	char cName[PROPERTY_NAME_SIZE];

	union {
		int32_t integer;
		uint64_t object;
		float floating;
	};
};

enum ObjectType : uint8_t {
	OBJ_NONE,

	OBJ_STYLE,
	OBJ_COMMENT,

	OBJ_VAR_COLOR = 0x40,
	OBJ_VAR_INT,
	OBJ_VAR_TEXT_STYLE,
	OBJ_VAR_ICON_STYLE,
	
	OBJ_PAINT_OVERWRITE = 0x60,
	OBJ_PAINT_LINEAR_GRADIENT,
	OBJ_PAINT_RADIAL_GRADIENT,

	OBJ_LAYER_BOX = 0x80,
	OBJ_LAYER_METRICS,
	OBJ_LAYER_TEXT,
	OBJ_LAYER_GROUP,
	// OBJ_LAYER_PATH,

	OBJ_MOD_COLOR = 0xC0,
	OBJ_MOD_MULTIPLY,
};

struct Object {
	ObjectType type;
#define OBJECT_NAME_SIZE (46)
	char cName[OBJECT_NAME_SIZE];
#define OBJECT_IS_SELECTED (1 << 0)
	uint8_t flags;
	uint64_t id;
	Array<Property> properties;
};

enum StepType : uint8_t {
	STEP_GROUP_MARKER,
	STEP_MODIFY_PROPERTY,
	STEP_RENAME_OBJECT,
	STEP_ADD_OBJECT,
	STEP_DELETE_OBJECT,
};

enum StepApplyMode {
	STEP_APPLY_NORMAL,
	STEP_APPLY_GROUPED,
	STEP_APPLY_UNDO,
	STEP_APPLY_REDO,
};

struct Step {
	StepType type;
#define STEP_UPDATE_INSPECTOR (1 << 0)
	uint32_t flags;
	uint64_t objectID;

	union {
		Property property;
		char cName[OBJECT_NAME_SIZE];
		Object object;
	};
};

Array<Step> undoStack;
Array<Step> redoStack;
bool documentModified;

// Document state:
Array<Object> objects;
uint64_t objectIDAllocator;

Object *ObjectFind(uint64_t id) {
	// TODO Use a hash table.

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		if (objects[i].id == id) {
			return &objects[i];
		}
	}

	return nullptr;
}

void ObjectSetSelected(uint64_t id, bool removeSelectedFlagFromPreviousSelection = true) {
	if (selectedObjectID && removeSelectedFlagFromPreviousSelection) {
		Object *object = ObjectFind(selectedObjectID);
		if (object) object->flags &= ~OBJECT_IS_SELECTED;
	}

	selectedObjectID = id;

	if (selectedObjectID) {
		Object *object = ObjectFind(selectedObjectID);
		if (object) object->flags |= OBJECT_IS_SELECTED;
	}
}

Property *PropertyFind(Object *object, const char *cName, uint8_t type = 0) {
	if (object) {
		for (uintptr_t i = 0; i < object->properties.Length(); i++) {
			if (0 == strcmp(object->properties[i].cName, cName)) {
				if (type && object->properties[i].type != type) {
					return nullptr;
				} else {
					return &object->properties[i];
				}
			}
		}
	}

	return nullptr;
}

int32_t PropertyReadInt32(Object *object, const char *cName, int32_t defaultValue = 0) {
	Property *property = PropertyFind(object, cName);
	return !property || property->type == PROP_OBJECT ? defaultValue : property->integer;
}

bool ObjectIsConditional(Object *object) {
	return PropertyReadInt32(object, "_primaryState")
		|| PropertyReadInt32(object, "_stateFocused")
		|| PropertyReadInt32(object, "_stateChecked")
		|| PropertyReadInt32(object, "_stateIndeterminate")
		|| PropertyReadInt32(object, "_stateDefaultButton")
		|| PropertyReadInt32(object, "_stateSelected")
		|| PropertyReadInt32(object, "_stateFocusedItem")
		|| PropertyReadInt32(object, "_stateListFocused")
		|| PropertyReadInt32(object, "_stateBeforeEnter")
		|| PropertyReadInt32(object, "_stateAfterExit");
}

Property *PropertyFindOrInherit(bool first, Object *object, const char *cName, uint8_t type = 0) {
	while (object) {
		if (first || !ObjectIsConditional(object)) {
			// Return the value if the object has this property.
			Property *property = PropertyFind(object, cName);
			if (property) return type && property->type != type ? nullptr : property;
		} else {
			// Skip the object if it has a condition and it's not the original object.
		}

		// Go to the inheritance parent object.
		Property *property = PropertyFind(object, "_parent", PROP_OBJECT);
		object = ObjectFind(property ? property->object : 0);
		first = false;
	}

	return nullptr;
}

void DocumentSave(void *) {
#ifdef OS_ESSENCE
	EsBuffer buffer = { .canGrow = 1 };
#define fwrite(a, b, c, d) EsBufferWrite(&buffer, (a), (b) * (c))
#else
	FILE *f = fopen("bin/designer2.dat", "wb");
#endif

	uint32_t version = 1;
	fwrite(&version, 1, sizeof(uint32_t), f);
	uint32_t objectCount = objects.Length();
	fwrite(&objectCount, 1, sizeof(uint32_t), f);
	fwrite(&objectIDAllocator, 1, sizeof(uint64_t), f);

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		Object copy = objects[i];
		uint32_t propertyCount = copy.properties.Length();
		copy.properties.array = nullptr;
		fwrite(&copy, 1, sizeof(Object), f);
		fwrite(&propertyCount, 1, sizeof(uint32_t), f);
		fwrite(objects[i].properties.array, 1, sizeof(Property) * propertyCount, f);
	}

#ifdef OS_ESSENCE
	EsFileStoreWriteAll(fileStore, buffer.out, buffer.position); 
	EsHeapFree(buffer.out);
#else
	fclose(f);
#endif
	documentModified = false;
}

void DocumentLoad() {
#ifdef OS_ESSENCE
	EsBuffer buffer = {};
	buffer.out = (uint8_t *) EsFileStoreReadAll(fileStore, &buffer.bytes);
#define fread(a, b, c, d) EsBufferReadInto(&buffer, (a), (b) * (c))
#else
	FILE *f = fopen("bin/designer2.dat", "rb");
	if (!f) return;
#endif

	uint32_t version = 1;
	fread(&version, 1, sizeof(uint32_t), f);
	uint32_t objectCount = 0;
	fread(&objectCount, 1, sizeof(uint32_t), f);
	fread(&objectIDAllocator, 1, sizeof(uint64_t), f);

	for (uintptr_t i = 0; i < objectCount; i++) {
		Object object = {};
		fread(&object, 1, sizeof(Object), f);
		uint32_t propertyCount = 0;
		fread(&propertyCount, 1, sizeof(uint32_t), f);
		object.properties.InsertMany(0, propertyCount);
		fread(object.properties.array, 1, sizeof(Property) * propertyCount, f);
		objects.Add(object);
	}

#ifdef OS_ESSENCE
	EsHeapFree(buffer.out);
#else
	fclose(f);
#endif
}

void DocumentFree() {
	for (uintptr_t i = 0; i < objects.Length(); i++) {
		objects[i].properties.Free();
	}

	objects.Free();
	undoStack.Free();
	redoStack.Free();
}

void DocumentApplyStep(Step step, StepApplyMode mode = STEP_APPLY_NORMAL) {
	bool allowMerge = false;

	if (step.type == STEP_GROUP_MARKER) {
	} else if (step.type == STEP_MODIFY_PROPERTY) {
		Object *object = ObjectFind(step.objectID);
		UI_ASSERT(object);

		Property *property = PropertyFind(object, step.property.cName);

		if (property) {
			Property oldValue = *property;

			if (step.property.type != PROP_NONE) {
				// Update the property.
				*property = step.property;
				allowMerge = true;
			} else {
				// Remove the property.
				for (uintptr_t i = 0; i < object->properties.Length(); i++) {
					if (0 == strcmp(object->properties[i].cName, step.property.cName)) {
						object->properties.DeleteSwap(i);
						break;
					}
				}
			}

			step.property = oldValue;
		} else {
			if (step.property.type != PROP_NONE) {
				// Add the property.
				object->properties.Add(step.property);
				step.property.type = PROP_NONE;
			} else {
				// Asking to remove a property that does not exist.
				// Probably from a remove broadcast.
			}
		}

		UIElementRepaint(canvas, nullptr);

		if (step.flags & STEP_UPDATE_INSPECTOR) {
			InspectorPopulate();
		} else {
			InspectorAnnouncePropertyChanged(step.objectID, step.property.cName);
		}
	} else if (step.type == STEP_RENAME_OBJECT) {
		Object *object = ObjectFind(step.objectID);
		UI_ASSERT(object);

		char oldName[OBJECT_NAME_SIZE];
		strcpy(oldName, object->cName);
		strcpy(object->cName, step.cName);
		strcpy(step.cName, oldName);

		UIElementRepaint(canvas, nullptr);
		InspectorPopulate();
	} else if (step.type == STEP_ADD_OBJECT) {
		objects.Add(step.object);
		UIElementRepaint(canvas, nullptr);
		step.objectID = step.object.id;
		step.type = STEP_DELETE_OBJECT;
	} else if (step.type == STEP_DELETE_OBJECT) {
		if (selectedObjectID == step.objectID) {
			ObjectSetSelected(0);
		}

		step.type = STEP_ADD_OBJECT;

		for (uintptr_t i = 0; i < objects.Length(); i++) {
			if (objects[i].id == step.objectID) {
				step.object = objects[i];
				objects.DeleteSwap(i);
				break;
			}
		}

		step.object.flags = 0;
		UIElementRepaint(canvas, nullptr);
		InspectorPopulate();
	} else {
		UI_ASSERT(false);
	}

	if (mode == STEP_APPLY_NORMAL || mode == STEP_APPLY_GROUPED) {
		bool merge = false;

		if (allowMerge && undoStack.Length() > 2 && !redoStack.Length() && (~step.flags & STEP_UPDATE_INSPECTOR)) {
			Step last = undoStack[undoStack.Length() - (undoStack.Last().type == STEP_GROUP_MARKER ? 2 : 1)];

			if (step.type == STEP_MODIFY_PROPERTY && last.type == STEP_MODIFY_PROPERTY 
					&& last.objectID == step.objectID && 0 == strcmp(last.property.cName, step.property.cName)) {
				merge = true;
			}
		}

		if (!merge) {
			undoStack.Add(step);
			
			for (uintptr_t i = 0; i < redoStack.Length(); i++) {
				if (redoStack[i].type == STEP_ADD_OBJECT) {
					redoStack[i].object.properties.Free();
				}
			}

			redoStack.Free();

			if (mode != STEP_APPLY_GROUPED) {
				Step step = {};
				step.type = STEP_GROUP_MARKER;
				undoStack.Add(step);
			}
		}
	} else if (mode == STEP_APPLY_UNDO) {
		redoStack.Add(step);
	} else if (mode == STEP_APPLY_REDO) {
		undoStack.Add(step);
	}

	documentModified = true;
	InspectorPickTargetEnd();

#ifdef UI_ESSENCE
	EsInstanceSetModified(ui.instance, true);
#endif
}

void DocumentUndoStep(void *) {
	if (!undoStack.Length()) return;

	Step marker = undoStack.Pop();
	UI_ASSERT(marker.type == STEP_GROUP_MARKER);

	do {
		DocumentApplyStep(undoStack.Pop(), STEP_APPLY_UNDO);
	} while (undoStack.Length() && undoStack.Last().type != STEP_GROUP_MARKER);

	redoStack.Add(marker);
}

void DocumentRedoStep(void *) {
	if (!redoStack.Length()) return;

	Step marker = redoStack.Pop();
	UI_ASSERT(marker.type == STEP_GROUP_MARKER);

	do {
		DocumentApplyStep(redoStack.Pop(), STEP_APPLY_REDO);
	} while (redoStack.Length() && redoStack.Last().type != STEP_GROUP_MARKER);

	undoStack.Add(marker);
}

void DocumentSwapPropertyPrefixes(Object *object, Step step, const char *cPrefix0, const char *cPrefix1, bool last, bool moveOnly) {
	char cNewName[PROPERTY_NAME_SIZE];
	Array<Step> steps = {};

	for (uintptr_t i = 0; i < object->properties.Length(); i++) {
		if (0 == memcmp(object->properties[i].cName, cPrefix0, strlen(cPrefix0))
				|| 0 == memcmp(object->properties[i].cName, cPrefix1, strlen(cPrefix1))) {
			strcpy(step.property.cName, object->properties[i].cName);
			step.property.type = PROP_NONE;
			steps.Add(step);
		}
	}

	for (uintptr_t i = 0; i < object->properties.Length(); i++) {
		if (!moveOnly && 0 == memcmp(object->properties[i].cName, cPrefix0, strlen(cPrefix0))) {
			strcpy(cNewName, cPrefix1);
			strcat(cNewName, object->properties[i].cName + strlen(cPrefix0));
			step.property = object->properties[i];
			strcpy(step.property.cName, cNewName);
			steps.Add(step);
		} else if (0 == memcmp(object->properties[i].cName, cPrefix1, strlen(cPrefix1))) {
			strcpy(cNewName, cPrefix0);
			strcat(cNewName, object->properties[i].cName + strlen(cPrefix1));
			step.property = object->properties[i];
			strcpy(step.property.cName, cNewName);
			steps.Add(step);
		}
	}

	for (uintptr_t i = 0; i < steps.Length(); i++) {
		DocumentApplyStep(steps[i], (i == steps.Length() - 1 && last) ? STEP_APPLY_NORMAL : STEP_APPLY_GROUPED);
	}

	steps.Free();
}

//////////////////////////////////////////////////////////////

enum InspectorElementType {
	INSPECTOR_INVALID_ELEMENT,
	INSPECTOR_REMOVE_BUTTON,
	INSPECTOR_REMOVE_BUTTON_BROADCAST,
	INSPECTOR_COLOR_PICKER,
	INSPECTOR_COLOR_TEXTBOX,
	INSPECTOR_INTEGER_TEXTBOX,
	INSPECTOR_INTEGER_TEXTBOX_BROADCAST,
	INSPECTOR_FLOAT_TEXTBOX,
	INSPECTOR_LINK,
	INSPECTOR_LINK_BROADCAST,
	INSPECTOR_BOOLEAN_TOGGLE,
	INSPECTOR_RADIO_SWITCH,
	INSPECTOR_CURSOR_DROP_DOWN,
	INSPECTOR_ADD_ARRAY_ITEM,
	INSPECTOR_SWAP_ARRAY_ITEMS,
	INSPECTOR_DELETE_ARRAY_ITEM,
};

struct InspectorBindingData {
	UIElement *element; 
	uint64_t objectID; 
	char cPropertyName[PROPERTY_NAME_SIZE];
	const char *cEnablePropertyName;
	InspectorElementType elementType;
	int32_t radioValue;
};

Array<UIElement *> inspectorBoundElements;
UIElement *inspectorActivePropertyStepElement;
InspectorBindingData *inspectorMenuData;
InspectorBindingData *inspectorPickData;

void InspectorPickTargetEnd() {
	if (inspectorPickData) {
		inspectorPickData = nullptr;
		UILabelSetContent(labelMessage, "", -1);
		UIElementRefresh(&labelMessage->e);
		UIElementRepaint(canvas, nullptr);
	}
}

void InspectorUpdateSingleElementEnable(InspectorBindingData *data) {
	UI_ASSERT(data->cEnablePropertyName);
	bool enabled = PropertyReadInt32(ObjectFind(data->objectID), data->cEnablePropertyName);
	if (enabled) data->element->flags &= ~UI_ELEMENT_DISABLED;
	else data->element->flags |= UI_ELEMENT_DISABLED;
	UIElementRefresh(data->element);
}

void InspectorUpdateSingleElement(InspectorBindingData *data) {
	if (data->element->flags & UI_ELEMENT_DESTROY) {
		return;
	}

	if (data->elementType == INSPECTOR_REMOVE_BUTTON || data->elementType == INSPECTOR_REMOVE_BUTTON_BROADCAST) {
		UIButton *button = (UIButton *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID), data->cPropertyName);
		if (property) button->e.flags &= ~UI_ELEMENT_DISABLED;
		else button->e.flags |= UI_ELEMENT_DISABLED;
		button->label[0] = property ? 'X' : '-';
		UIElementRefresh(&button->e);
	} else if (data->elementType == INSPECTOR_BOOLEAN_TOGGLE) {
		UICheckbox *box = (UICheckbox *) data->element;
		box->check = PropertyReadInt32(ObjectFind(data->objectID), data->cPropertyName, 2);
		if ((~box->e.flags & UI_CHECKBOX_ALLOW_INDETERMINATE)) box->check &= 1;
		UIElementRefresh(&box->e);
	} else if (data->elementType == INSPECTOR_RADIO_SWITCH) {
		UIButton *button = (UIButton *) data->element;
		int32_t value = PropertyReadInt32(ObjectFind(data->objectID), data->cPropertyName);
		if (value == data->radioValue) button->e.flags |= UI_BUTTON_CHECKED;
		else button->e.flags &= ~UI_BUTTON_CHECKED;
		UIElementRefresh(&button->e);
	} else if (data->elementType == INSPECTOR_CURSOR_DROP_DOWN) {
		UIButton *button = (UIButton *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID), data->cPropertyName, PROP_INT);
		UI_FREE(button->label);
		button->label = UIStringCopy(property ? cCursorStrings[property->integer] : "---", (button->labelBytes = -1));
		UIElementRefresh(&button->e);
	} else if (data->elementType == INSPECTOR_COLOR_PICKER) {
		UIColorPicker *colorPicker = (UIColorPicker *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID), data->cPropertyName, PROP_COLOR);
		uint32_t color = property ? property->integer : 0xFFFFFFFF;
		colorPicker->opacity = (color >> 24) / 255.0f;
		UIColorToHSV(color, &colorPicker->hue, &colorPicker->saturation, &colorPicker->value);
		UIElementRefresh(&colorPicker->e);
	} else if (data->elementType == INSPECTOR_COLOR_TEXTBOX) {
		UITextbox *textbox = (UITextbox *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID), data->cPropertyName, PROP_COLOR);
		char buffer[32] = "";
		if (property) snprintf(buffer, sizeof(buffer), "%.8X", (uint32_t) property->integer);
		UITextboxClear(textbox, false);
		UITextboxReplace(textbox, buffer, -1, false);
		UIElementRefresh(&textbox->e);
	} else if (data->elementType == INSPECTOR_INTEGER_TEXTBOX || data->elementType == INSPECTOR_INTEGER_TEXTBOX_BROADCAST) {
		UITextbox *textbox = (UITextbox *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID), data->cPropertyName, PROP_INT);
		char buffer[32] = "";
		if (property) snprintf(buffer, sizeof(buffer), "%d", property->integer);
		UITextboxClear(textbox, false);
		UITextboxReplace(textbox, buffer, -1, false);
		UIElementRefresh(&textbox->e);
	} else if (data->elementType == INSPECTOR_FLOAT_TEXTBOX) {
		UITextbox *textbox = (UITextbox *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID), data->cPropertyName, PROP_FLOAT);
		char buffer[32] = "";
		if (property) snprintf(buffer, sizeof(buffer), "%f", property->floating);
		UITextboxClear(textbox, false);
		UITextboxReplace(textbox, buffer, -1, false);
		UIElementRefresh(&textbox->e);
	} else if (data->elementType == INSPECTOR_LINK || data->elementType == INSPECTOR_LINK_BROADCAST) {
		UIButton *button = (UIButton *) data->element;
		Property *property = PropertyFind(ObjectFind(data->objectID), data->cPropertyName, PROP_OBJECT);
		Object *target = ObjectFind(property ? property->object : 0);
		const char *string = target ? (target->cName[0] ? target->cName : "(untitled)") : "---";
		UI_FREE(button->label);
		button->label = UIStringCopy(string, (button->labelBytes = -1));
		UIElementRefresh(&button->e);
	} else if (data->elementType == INSPECTOR_ADD_ARRAY_ITEM || data->elementType == INSPECTOR_SWAP_ARRAY_ITEMS 
			|| data->elementType == INSPECTOR_DELETE_ARRAY_ITEM) {
	} else {
		UI_ASSERT(false);
	}
}

void InspectorAnnouncePropertyChanged(uint64_t objectID, const char *cPropertyName) {
	for (uintptr_t i = 0; i < inspectorBoundElements.Length(); i++) {
		InspectorBindingData *data = (InspectorBindingData *) inspectorBoundElements[i]->cp;
		if (data->element == inspectorActivePropertyStepElement) continue;
		if (data->objectID != objectID) continue; 

		if (data->cEnablePropertyName && 0 == strcmp(data->cEnablePropertyName, cPropertyName)) {
			InspectorUpdateSingleElementEnable(data);
		}

		if (0 == strcmp(data->cPropertyName, cPropertyName)) {
			InspectorUpdateSingleElement(data);
		}
	}
}

void InspectorBroadcastStep(Step step, InspectorBindingData *data) {
	if (data->elementType == INSPECTOR_INTEGER_TEXTBOX_BROADCAST || data->elementType == INSPECTOR_LINK_BROADCAST
			|| data->elementType == INSPECTOR_REMOVE_BUTTON_BROADCAST) {
		for (char i = '1'; i <= '3'; i++) {
			step.property.cName[strlen(step.property.cName) - 1] = i;
			DocumentApplyStep(step, STEP_APPLY_GROUPED);
		}

		strcpy(step.property.cName, data->cPropertyName);
	}
}

void InspectorCursorDropDownMenuInvoke(void *cp) {
	intptr_t index = (intptr_t) cp;
	Step step = {};
	step.type = STEP_MODIFY_PROPERTY;
	step.objectID = inspectorMenuData->objectID;
	strcpy(step.property.cName, inspectorMenuData->cPropertyName);
	step.property.type = index == -1 ? PROP_NONE : PROP_INT;
	step.property.integer = index;
	DocumentApplyStep(step);
	inspectorMenuData = nullptr;
}

int InspectorBoundMessage(UIElement *element, UIMessage message, int di, void *dp) {
	InspectorBindingData *data = (InspectorBindingData *) element->cp;

	if (message == UI_MSG_DESTROY) {
		inspectorBoundElements.FindAndDeleteSwap(element, true);
		free(data);
		data = nullptr;
	} else if (message == UI_MSG_VALUE_CHANGED) {
		Step step = {};
		step.type = STEP_MODIFY_PROPERTY;
		step.objectID = data->objectID;
		strcpy(step.property.cName, data->cPropertyName);

		if (data->elementType == INSPECTOR_COLOR_PICKER) {
			UIColorPicker *colorPicker = (UIColorPicker *) element;
			uint32_t color;
			UIColorToRGB(colorPicker->hue, colorPicker->saturation, colorPicker->value, &color);
			color |= (uint32_t) (colorPicker->opacity * 255.0f) << 24;
			step.property.type = PROP_COLOR;
			step.property.integer = (int32_t) color;
		} else if (data->elementType == INSPECTOR_COLOR_TEXTBOX) {
			UITextbox *textbox = (UITextbox *) element;
			char buffer[32];
			int length = 31 > textbox->bytes ? textbox->bytes : 31;
			memcpy(buffer, textbox->string, length);
			buffer[length] = 0;
			step.property.type = PROP_COLOR;
			step.property.integer = (int32_t) strtoul(buffer, nullptr, 16);
		} else if (data->elementType == INSPECTOR_INTEGER_TEXTBOX || data->elementType == INSPECTOR_INTEGER_TEXTBOX_BROADCAST) {
			UITextbox *textbox = (UITextbox *) element;
			char buffer[32];
			int length = 31 > textbox->bytes ? textbox->bytes : 31;
			memcpy(buffer, textbox->string, length);
			buffer[length] = 0;
			step.property.type = PROP_INT;
			step.property.integer = (int32_t) strtol(buffer, nullptr, 0);
			InspectorBroadcastStep(step, data);
		} else if (data->elementType == INSPECTOR_FLOAT_TEXTBOX) {
			UITextbox *textbox = (UITextbox *) element;
			char buffer[32];
			int length = 31 > textbox->bytes ? textbox->bytes : 31;
			memcpy(buffer, textbox->string, length);
			buffer[length] = 0;
			step.property.type = PROP_FLOAT;
			step.property.floating = strtof(buffer, nullptr);
			InspectorBroadcastStep(step, data);
		} else {
			UI_ASSERT(false);
		}

		inspectorActivePropertyStepElement = element; // Don't tell this element about the step.
		DocumentApplyStep(step);
		inspectorActivePropertyStepElement = nullptr;
	} else if (message == UI_MSG_CLICKED) {
		Step step = {};
		step.type = STEP_MODIFY_PROPERTY;
		step.objectID = data->objectID;
		strcpy(step.property.cName, data->cPropertyName);

		if (data->elementType == INSPECTOR_REMOVE_BUTTON || data->elementType == INSPECTOR_REMOVE_BUTTON_BROADCAST) {
			step.property.type = PROP_NONE; // Remove the property.
			InspectorBroadcastStep(step, data);
			DocumentApplyStep(step);
		} else if (data->elementType == INSPECTOR_LINK || data->elementType == INSPECTOR_LINK_BROADCAST) {
			char *name = nullptr;
			const char *dialog = "Enter the name of the new link target object:\n%t\n\n%l\n\n%f%b%b";
			const char *result = UIDialogShow(window, 0, dialog, &name, "OK", "Cancel");

			if (0 == strcmp(result, "OK")) {
				uint64_t id = 0;

				for (uintptr_t i = 0; i < objects.Length(); i++) {
					if (0 == strcmp(objects[i].cName, name)) {
						id = objects[i].id;
						break;
					}
				}

				if (!id) {
					UIDialogShow(window, 0, "Error: The object was not found.\n%f%b", "OK");
				} else {
					step.property.type = PROP_OBJECT;
					step.property.object = id;
					InspectorBroadcastStep(step, data);
					DocumentApplyStep(step);
				}
			}

			free(name);
		} else if (data->elementType == INSPECTOR_RADIO_SWITCH) {
			step.property.type = PROP_INT;
			step.property.integer = data->radioValue;
			DocumentApplyStep(step);
		} else if (data->elementType == INSPECTOR_BOOLEAN_TOGGLE) {
			UICheckbox *box = (UICheckbox *) element;
			step.property.integer = (box->check + 1) % ((box->e.flags & UI_CHECKBOX_ALLOW_INDETERMINATE) ? 3 : 2);
			step.property.type = step.property.integer == UI_CHECK_INDETERMINATE ? PROP_NONE : PROP_INT;
			DocumentApplyStep(step);
			return 1; // InspectorUpdateSingleElement will update the check.
		} else if (data->elementType == INSPECTOR_CURSOR_DROP_DOWN) {
			UIMenu *menu = UIMenuCreate(window->pressed, UI_MENU_NO_SCROLL);
			UIMenuAddItem(menu, 0, "Inherit", -1, InspectorCursorDropDownMenuInvoke, (void *) (intptr_t) -1);

			for (uintptr_t i = 0; i < sizeof(cCursorStrings) / sizeof(cCursorStrings[0]); i++) {
				UIMenuAddItem(menu, 0, cCursorStrings[i], -1, InspectorCursorDropDownMenuInvoke, (void *) (intptr_t) i);
			}

			inspectorMenuData = data;
			UIMenuShow(menu);
		} else if (data->elementType == INSPECTOR_ADD_ARRAY_ITEM) {
			step.property.type = PROP_INT;
			step.property.integer = 1 + PropertyReadInt32(ObjectFind(data->objectID), data->cPropertyName);
			step.flags |= STEP_UPDATE_INSPECTOR;
			DocumentApplyStep(step);
		} else if (data->elementType == INSPECTOR_SWAP_ARRAY_ITEMS) {
			char cPrefix0[PROPERTY_NAME_SIZE];
			char cPrefix1[PROPERTY_NAME_SIZE];
			Object *object = ObjectFind(data->objectID);

			strcpy(cPrefix0, data->cPropertyName);
			strcpy(cPrefix1, data->cPropertyName);

			for (intptr_t i = strlen(cPrefix0) - 2; i >= 0; i--) {
				if (cPrefix0[i] == '_') {
					int32_t index = atoi(cPrefix0 + i + 1);
					sprintf(cPrefix1 + i + 1, "%d_", index + 1);
					break;
				}
			}

			DocumentSwapPropertyPrefixes(object, step, cPrefix0, cPrefix1, true /* last */, false);
		} else if (data->elementType == INSPECTOR_DELETE_ARRAY_ITEM) {
			char cPrefix0[PROPERTY_NAME_SIZE];
			char cPrefix1[PROPERTY_NAME_SIZE];
			int32_t index = -1;
			int32_t count = -1;
			intptr_t offset = strlen(data->cPropertyName) - 2;
			Object *object = ObjectFind(data->objectID);

			for (; offset >= 0; offset--) {
				if (data->cPropertyName[offset] == '_') {
					index = atoi(data->cPropertyName + offset + 1);
					break;
				}
			}

			strcpy(cPrefix0, data->cPropertyName);
			strcpy(cPrefix0 + offset + 1, "count");
			count = PropertyReadInt32(ObjectFind(data->objectID), cPrefix0);

			for (int32_t i = index; i < count - 1; i++) {
				strcpy(cPrefix0, data->cPropertyName);
				strcpy(cPrefix1, data->cPropertyName);
				sprintf(cPrefix0 + offset + 1, "%d_", i + 0);
				sprintf(cPrefix1 + offset + 1, "%d_", i + 1);
				DocumentSwapPropertyPrefixes(object, step, cPrefix0, cPrefix1, false, true /* moveOnly */);
			}

			strcpy(cPrefix0, data->cPropertyName);
			sprintf(cPrefix0 + offset + 1, "%d_", count - 1);

			Array<Step> steps = {};

			for (uintptr_t i = 0; i < object->properties.Length(); i++) {
				if (0 == memcmp(object->properties[i].cName, cPrefix0, strlen(cPrefix0))) {
					strcpy(step.property.cName, object->properties[i].cName);
					step.property.type = PROP_NONE;
					steps.Add(step);
				}
			}

			for (uintptr_t i = 0; i < steps.Length(); i++) {
				DocumentApplyStep(steps[i], STEP_APPLY_GROUPED);
			}

			steps.Free();

			strcpy(step.property.cName, data->cPropertyName);
			strcpy(step.property.cName + offset + 1, "count");
			step.property.type = PROP_INT;
			step.property.integer = count - 1;
			step.flags |= STEP_UPDATE_INSPECTOR;
			DocumentApplyStep(step);
		}
	} else if (message == UI_MSG_UPDATE) {
		if (di == UI_UPDATE_FOCUSED && element->window->focused == element 
				&& (data->elementType == INSPECTOR_COLOR_TEXTBOX || data->elementType == INSPECTOR_INTEGER_TEXTBOX
					|| data->elementType == INSPECTOR_INTEGER_TEXTBOX_BROADCAST || data->elementType == INSPECTOR_FLOAT_TEXTBOX)) {
			UITextbox *textbox = (UITextbox *) element;
			textbox->carets[0] = 0;
			textbox->carets[1] = textbox->bytes;
		}
	}

	return 0;
}

InspectorBindingData *InspectorBind(UIElement *element, uint64_t objectID, const char *cPropertyName, InspectorElementType elementType, 
		int32_t radioValue = 0, const char *cEnablePropertyName = nullptr) {
	InspectorBindingData *data = (InspectorBindingData *) calloc(1, sizeof(InspectorBindingData));
	data->element = element;
	data->objectID = objectID;
	strcpy(data->cPropertyName, cPropertyName);
	data->elementType = elementType;
	data->radioValue = radioValue;
	data->cEnablePropertyName = cEnablePropertyName;
	element->cp = data;
	element->messageUser = InspectorBoundMessage;
	inspectorBoundElements.Add(element);
	InspectorUpdateSingleElement(data);
	if (cEnablePropertyName) InspectorUpdateSingleElementEnable(data);
	return data;
}

void InspectorRenameObject(void *) {
	Step step = {};
	step.type = STEP_RENAME_OBJECT;
	step.objectID = selectedObjectID;

	char *name = nullptr;
	const char *result = UIDialogShow(window, 0, "Enter the new name for the object:\n%t\n\n%l\n\n%f%b%b", &name, "OK", "Cancel");

	if (0 == strcmp(result, "OK")) {
		if (name && strlen(name) >= sizeof(step.cName) - 1) {
			UIDialogShow(window, 0, "Error: Name cannot have more than 46 characters.\n%f%b", "OK");
		} else {
			strcpy(step.cName, name ?: "");
			DocumentApplyStep(step);
		}
	}

	free(name);
}

void InspectorAutoNameObject(void *) {
	Object *object = ObjectFind(selectedObjectID);

	if (!ObjectIsConditional(object)) {
		UIDialogShow(window, 0, "Error: The object needs to be conditional to use auto-name.\n%f%b", "OK");
		return;
	}

	int32_t primaryState = PropertyReadInt32(object, "_primaryState");

	int32_t stateBits[] = { 
		PropertyReadInt32(object, "_stateFocused"),
		PropertyReadInt32(object, "_stateChecked"),
		PropertyReadInt32(object, "_stateIndeterminate"),
		PropertyReadInt32(object, "_stateDefaultButton"),
		PropertyReadInt32(object, "_stateSelected"),
		PropertyReadInt32(object, "_stateFocusedItem"),
		PropertyReadInt32(object, "_stateListFocused"),
		PropertyReadInt32(object, "_stateBeforeEnter"),
		PropertyReadInt32(object, "_stateAfterExit"),
	};

	Step step = {};
	step.type = STEP_RENAME_OBJECT;
	step.objectID = selectedObjectID;

	snprintf(step.cName, sizeof(step.cName), "?%s", primaryState ? cPrimaryStateStrings[primaryState] : "");

	for (uintptr_t i = 0; i < sizeof(stateBits) / sizeof(stateBits[0]); i++) {
		if (stateBits[i]) {
			snprintf(step.cName + strlen(step.cName), sizeof(step.cName) - strlen(step.cName), "%s%s", i || primaryState ? "&" : "", cStateBitStrings[i]);
		}
	}

	DocumentApplyStep(step);
}

void InspectorPickTargetCommand(void *cp) {
	if (inspectorPickData) {
		InspectorPickTargetEnd();
		return;
	}

	inspectorPickData = (InspectorBindingData *) cp;
	UILabelSetContent(labelMessage, "** Click an object to link it. **", -1);
	UIElementRefresh(&labelMessage->e);
	UIElementRepaint(canvas, nullptr);
}

void InspectorFindTargetCommand(void *cp) {
	InspectorBindingData *data = (InspectorBindingData *) cp;
	Property *property = PropertyFind(ObjectFind(data->objectID), data->cPropertyName, PROP_OBJECT);
	Object *target = ObjectFind(property ? property->object : 0);

	if (target) {
		CanvasSelectObject(target);
	} else {
		UIDialogShow(window, 0, "Error: The object does not exist.\n%f%b", "OK");
	}
}

int InspectorTabPaneMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_LEFT_DOWN) {
		element->messageClass(element, message, di, dp);
		UIElementRefresh(inspector);
		return 1;
	}

	return 0;
}

void InspectorAddLink(Object *object, const char *cLabel, const char *cPropertyName, 
		bool broadcast = false, const char *cEnablePropertyName = nullptr) {
	if (cLabel) UILabelCreate(0, 0, cLabel, -1);
	UIElement *row = &UIPanelCreate(0, UI_PANEL_HORIZONTAL)->e;
	InspectorBindingData *data = InspectorBind(&UIButtonCreate(row, UI_ELEMENT_H_FILL, 0, 0)->e, 
			object->id, cPropertyName, broadcast ? INSPECTOR_LINK_BROADCAST : INSPECTOR_LINK, 0, cEnablePropertyName);
	UIButton *pickTarget = UIButtonCreate(row, UI_BUTTON_SMALL, "Pick", -1);
	pickTarget->e.cp = data;
	pickTarget->invoke = InspectorPickTargetCommand;
	UIButton *findTarget = UIButtonCreate(row, UI_BUTTON_SMALL, "Find", -1);
	findTarget->e.cp = data;
	findTarget->invoke = InspectorFindTargetCommand;
	if (!broadcast) InspectorBind(&UIButtonCreate(row, UI_BUTTON_SMALL, "X", 1)->e, object->id, cPropertyName, INSPECTOR_REMOVE_BUTTON);
}

void InspectorAddIntegerTextbox(Object *object, const char *cLabel, const char *cPropertyName, bool broadcast = false, const char *cEnablePropertyName = nullptr) {
	if (cLabel) UILabelCreate(0, 0, cLabel, -1);
	InspectorBind(&UITextboxCreate(0, UI_ELEMENT_H_FILL)->e, object->id, cPropertyName, 
			broadcast ? INSPECTOR_INTEGER_TEXTBOX_BROADCAST : INSPECTOR_INTEGER_TEXTBOX, 0, cEnablePropertyName);
}

void InspectorAddFloat(Object *object, const char *cLabel, const char *cPropertyName, const char *cEnablePropertyName = nullptr) {
	if (cLabel) UILabelCreate(0, 0, cLabel, -1);
	UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_H_FILL | UI_PANEL_HORIZONTAL);
	InspectorBind(&UITextboxCreate(0, UI_ELEMENT_H_FILL)->e, object->id, cPropertyName, INSPECTOR_FLOAT_TEXTBOX, 0, cEnablePropertyName);
	InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, cPropertyName, INSPECTOR_REMOVE_BUTTON);
	UIParentPop();
}

void InspectorAddInteger(Object *object, const char *cLabel, const char *cPropertyName) {
	if (cLabel) UILabelCreate(0, 0, cLabel, -1);
	UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_H_FILL | UI_PANEL_HORIZONTAL);
	UITabPane *tabPane = UITabPaneCreate(0, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_H_FILL, "Direct\tLink");
	tabPane->e.messageUser = InspectorTabPaneMessage;
	InspectorAddIntegerTextbox(object, nullptr, cPropertyName);
	InspectorAddLink(object, nullptr, cPropertyName);
	UIParentPop();
	Property *property = PropertyFind(object, cPropertyName);
	if (property && property->type == PROP_OBJECT) tabPane->active = 1;
	InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, cPropertyName, INSPECTOR_REMOVE_BUTTON);
	UIParentPop();
}

void InspectorAddFourGroup(Object *object, const char *cLabel, const char *cPropertyNamePrefix, 
		const char *cEnablePropertyName = nullptr, bool defaultToIndividualTab = false) {
	char cPropertyName0[PROPERTY_NAME_SIZE]; strcpy(cPropertyName0, cPropertyNamePrefix); strcat(cPropertyName0, "0");
	char cPropertyName1[PROPERTY_NAME_SIZE]; strcpy(cPropertyName1, cPropertyNamePrefix); strcat(cPropertyName1, "1");
	char cPropertyName2[PROPERTY_NAME_SIZE]; strcpy(cPropertyName2, cPropertyNamePrefix); strcat(cPropertyName2, "2");
	char cPropertyName3[PROPERTY_NAME_SIZE]; strcpy(cPropertyName3, cPropertyNamePrefix); strcat(cPropertyName3, "3");
	if (cLabel) UILabelCreate(0, 0, cLabel, -1);
	UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_H_FILL | UI_PANEL_HORIZONTAL);
	UITabPane *tabPane = UITabPaneCreate(0, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_H_FILL, "Single\tIndividual\tLink");
	tabPane->e.messageUser = InspectorTabPaneMessage;
	InspectorAddIntegerTextbox(object, nullptr, cPropertyName0, true /* broadcast */, cEnablePropertyName);
	UIElement *row = &UIPanelCreate(0, UI_PANEL_HORIZONTAL)->e;
	InspectorBind(&UITextboxCreate(row, UI_ELEMENT_H_FILL)->e, object->id, cPropertyName0, INSPECTOR_INTEGER_TEXTBOX, 0, cEnablePropertyName);
	InspectorBind(&UITextboxCreate(row, UI_ELEMENT_H_FILL)->e, object->id, cPropertyName1, INSPECTOR_INTEGER_TEXTBOX, 0, cEnablePropertyName);
	InspectorBind(&UITextboxCreate(row, UI_ELEMENT_H_FILL)->e, object->id, cPropertyName2, INSPECTOR_INTEGER_TEXTBOX, 0, cEnablePropertyName);
	InspectorBind(&UITextboxCreate(row, UI_ELEMENT_H_FILL)->e, object->id, cPropertyName3, INSPECTOR_INTEGER_TEXTBOX, 0, cEnablePropertyName);
	InspectorAddLink(object, nullptr, cPropertyName0, true /* broadcast */, cEnablePropertyName);
	UIParentPop();
	Property *property = PropertyFind(object, cPropertyName0);
	int32_t b0 = PropertyReadInt32(object, cPropertyName0);
	int32_t b1 = PropertyReadInt32(object, cPropertyName1);
	int32_t b2 = PropertyReadInt32(object, cPropertyName2);
	int32_t b3 = PropertyReadInt32(object, cPropertyName3);
	if (property && property->type == PROP_OBJECT) tabPane->active = 2;
	else if (defaultToIndividualTab || b0 != b1 || b1 != b2 || b2 != b3) tabPane->active = 1;
	InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, cPropertyName0, INSPECTOR_REMOVE_BUTTON_BROADCAST);
	UIParentPop();
}

void InspectorAddBooleanToggle(Object *object, const char *cLabel, const char *cPropertyName, bool allowIndeterminate = true) {
	InspectorBind(&UICheckboxCreate(0, allowIndeterminate ? UI_CHECKBOX_ALLOW_INDETERMINATE : 0, cLabel, -1)->e, 
			object->id, cPropertyName, INSPECTOR_BOOLEAN_TOGGLE);
}

void InspectorAddRadioSwitch(Object *object, const char *cLabel, const char *cPropertyName, int32_t radioValue) {
	InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, cLabel, -1)->e, object->id, cPropertyName, INSPECTOR_RADIO_SWITCH, radioValue);
}

void InspectorPopulate() {
	UIElementDestroyDescendents(inspector);
	UIParentPush(inspector);

	Object *object = ObjectFind(selectedObjectID);

	if (object) {
		UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_BORDER | UI_PANEL_MEDIUM_SPACING | UI_PANEL_EXPAND);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			char buffer[256];
			snprintf(buffer, sizeof(buffer), "%lu: %s", object->id, object->cName[0] ? object->cName : "(untitled)");
			UILabelCreate(0, 0, buffer, -1);
			UISpacerCreate(0, UI_ELEMENT_H_FILL, 0, 0);
			UIButtonCreate(0, UI_BUTTON_SMALL, "Auto", -1)->invoke = InspectorAutoNameObject;
			UIButtonCreate(0, UI_BUTTON_SMALL, "Rename", -1)->invoke = InspectorRenameObject;
			UIParentPop();

			bool inheritWithAnimation = object->type == OBJ_VAR_TEXT_STYLE || object->type == OBJ_VAR_ICON_STYLE 
				|| object->type == OBJ_PAINT_OVERWRITE || object->type == OBJ_LAYER_BOX || object->type == OBJ_LAYER_TEXT
				|| object->type == OBJ_PAINT_LINEAR_GRADIENT || object->type == OBJ_PAINT_RADIAL_GRADIENT;
			bool inheritWithoutAnimation = object->type == OBJ_STYLE || object->type == OBJ_LAYER_METRICS;

			if (inheritWithAnimation || inheritWithoutAnimation) {
				InspectorAddLink(object, "Inherit from:", "_parent");

				UILabelCreate(0, 0, "Primary state:", -1);
				UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
				InspectorAddRadioSwitch(object, "Idle", "_primaryState", THEME_PRIMARY_STATE_IDLE);
				InspectorAddRadioSwitch(object, "Hovered", "_primaryState", THEME_PRIMARY_STATE_HOVERED);
				InspectorAddRadioSwitch(object, "Pressed", "_primaryState", THEME_PRIMARY_STATE_PRESSED);
				InspectorAddRadioSwitch(object, "Disabled", "_primaryState", THEME_PRIMARY_STATE_DISABLED);
				InspectorAddRadioSwitch(object, "Inactive", "_primaryState", THEME_PRIMARY_STATE_INACTIVE);
				InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, "_primaryState", INSPECTOR_REMOVE_BUTTON);
				UIParentPop();

				// TODO Change these to be stored internally as a mask of bits.
				UILabelCreate(0, 0, "State bits:", -1);
				UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_EXPAND)->gap = -5;
				UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL)->gap = 8;
				InspectorAddBooleanToggle(object, cStateBitStrings[0], "_stateFocused", false);
				InspectorAddBooleanToggle(object, cStateBitStrings[1], "_stateChecked", false);
				InspectorAddBooleanToggle(object, cStateBitStrings[2], "_stateIndeterminate", false);
				InspectorAddBooleanToggle(object, cStateBitStrings[3], "_stateDefaultButton", false);
				InspectorAddBooleanToggle(object, cStateBitStrings[4], "_stateSelected", false);
				UIParentPop();
				UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL)->gap = 8;
				InspectorAddBooleanToggle(object, cStateBitStrings[5], "_stateFocusedItem", false);
				InspectorAddBooleanToggle(object, cStateBitStrings[6], "_stateListFocused", false);
				InspectorAddBooleanToggle(object, cStateBitStrings[7], "_stateBeforeEnter", false);
				InspectorAddBooleanToggle(object, cStateBitStrings[8], "_stateAfterExit", false);
				UIParentPop();
				UIParentPop();

				if (inheritWithAnimation) {
					UILabelCreate(0, 0, "Transition duration:", -1);
					InspectorAddInteger(object, nullptr, "_duration");
				}
			}
		UIParentPop();

		UISpacerCreate(0, 0, 0, 10);

		if (object->type == OBJ_STYLE) {
			InspectorAddLink(object, "Appearance:", "appearance");
			InspectorAddLink(object, "Metrics:", "metrics");
			InspectorAddLink(object, "Text style:", "textStyle");
			InspectorAddLink(object, "Icon style:", "iconStyle");
			InspectorAddBooleanToggle(object, "Public style", "isPublic");
		} else if (object->type == OBJ_VAR_COLOR) {
			InspectorBind(&UIColorPickerCreate(&UIPanelCreate(0, 0)->e, UI_COLOR_PICKER_HAS_OPACITY)->e, object->id, "color", INSPECTOR_COLOR_PICKER);
			InspectorBind(&UITextboxCreate(0, 0)->e, object->id, "color", INSPECTOR_COLOR_TEXTBOX);
			InspectorAddBooleanToggle(object, "Export to theme file", "isExported");
		} else if (object->type == OBJ_VAR_INT) {
			InspectorBind(&UITextboxCreate(0, 0)->e, object->id, "value", INSPECTOR_INTEGER_TEXTBOX);
			InspectorAddBooleanToggle(object, "Export to theme file", "isExported");
			InspectorAddBooleanToggle(object, "Apply UI scaling factor", "isScaled");
		} else if (object->type == OBJ_LAYER_METRICS) {
			UILabelCreate(0, 0, "General options:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddBooleanToggle(object, "Enable clipping", "clipEnabled");
			InspectorAddBooleanToggle(object, "Wrap text", "wrapText");
			InspectorAddBooleanToggle(object, "Ellipsis", "ellipsis");
			UIParentPop();

			InspectorAddFourGroup(object, "Insets:", "insets");
			InspectorAddFourGroup(object, "Clip insets:", "clipInsets", "clipEnabled");

			UILabelCreate(0, 0, "Preferred size:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddInteger(object, nullptr, "preferredWidth");
			InspectorAddInteger(object, nullptr, "preferredHeight");
			UIParentPop();

			UILabelCreate(0, 0, "Minimum size:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddInteger(object, nullptr, "minimumWidth");
			InspectorAddInteger(object, nullptr, "minimumHeight");
			UIParentPop();

			UILabelCreate(0, 0, "Maximum size:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddInteger(object, nullptr, "maximumWidth");
			InspectorAddInteger(object, nullptr, "maximumHeight");
			UIParentPop();

			UILabelCreate(0, 0, "Gaps:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddInteger(object, nullptr, "gapMajor");
			InspectorAddInteger(object, nullptr, "gapMinor");
			InspectorAddInteger(object, nullptr, "gapWrap");
			UIParentPop();

			UILabelCreate(0, 0, "Horizontal text alignment:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddRadioSwitch(object, "Left", "horizontalTextAlign", 1);
			InspectorAddRadioSwitch(object, "Center", "horizontalTextAlign", 2);
			InspectorAddRadioSwitch(object, "Right", "horizontalTextAlign", 3);
			InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, "horizontalTextAlign", INSPECTOR_REMOVE_BUTTON);
			UIParentPop();

			UILabelCreate(0, 0, "Vertical text alignment:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddRadioSwitch(object, "Top", "verticalTextAlign", 1);
			InspectorAddRadioSwitch(object, "Center", "verticalTextAlign", 2);
			InspectorAddRadioSwitch(object, "Bottom", "verticalTextAlign", 3);
			InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, "verticalTextAlign", INSPECTOR_REMOVE_BUTTON);
			UIParentPop();

			UILabelCreate(0, 0, "Cursor style:", -1);
			InspectorBind(&UIButtonCreate(0, UI_BUTTON_DROP_DOWN, 0, 0)->e, object->id, "cursor", INSPECTOR_CURSOR_DROP_DOWN);
		} else if (object->type == OBJ_VAR_TEXT_STYLE) {
			InspectorAddLink(object, "Text color:", "textColor");
			InspectorAddLink(object, "Selection background color:", "selectedBackground");
			InspectorAddLink(object, "Selection text color:", "selectedText");
			InspectorAddInteger(object, "Text size:", "textSize");
			InspectorAddInteger(object, "Font weight:", "fontWeight");

			UILabelCreate(0, 0, "Font options:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddBooleanToggle(object, "Italic", "isItalic");
			UIParentPop();

			UILabelCreate(0, 0, "Font family:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddRadioSwitch(object, "Sans-serif", "fontFamily", ES_FONT_SANS);
			InspectorAddRadioSwitch(object, "Serif", "fontFamily", ES_FONT_SERIF);
			InspectorAddRadioSwitch(object, "Monospaced", "fontFamily", ES_FONT_MONOSPACED);
			InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, "fontFamily", INSPECTOR_REMOVE_BUTTON);
			UIParentPop();
		} else if (object->type == OBJ_VAR_ICON_STYLE) {
			InspectorAddLink(object, "Icon color:", "iconColor");
			InspectorAddInteger(object, "Icon size:", "iconSize");
		} else if (object->type == OBJ_LAYER_BOX) {
			InspectorAddFourGroup(object, "Border sizes:", "borders");
			InspectorAddFourGroup(object, "Corner radii:", "corners");
			InspectorAddLink(object, "Fill paint:", "mainPaint");
			InspectorAddLink(object, "Border paint:", "borderPaint");

			InspectorAddBooleanToggle(object, "Blurred", "isBlurred");
			InspectorAddBooleanToggle(object, "Auto-corners", "autoCorners");
			InspectorAddBooleanToggle(object, "Auto-borders", "autoBorders");
			InspectorAddBooleanToggle(object, "Shadow hiding", "shadowHiding");
		} else if (object->type == OBJ_LAYER_TEXT) {
			InspectorAddLink(object, "Text color:", "color");
			InspectorAddInteger(object, "Blur radius:", "blur");
		} else if (object->type == OBJ_LAYER_GROUP) {
			int32_t layerCount = PropertyReadInt32(object, "layers_count");
			if (layerCount < 0) layerCount = 0;
			if (layerCount > 100) layerCount = 100;

			for (int32_t i = 0; i < layerCount; i++) {
				char cPropertyName[PROPERTY_NAME_SIZE];
				UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_BORDER | UI_PANEL_MEDIUM_SPACING | UI_PANEL_EXPAND);
				sprintf(cPropertyName, "layers_%d_", i);
				UIPanel *row = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
				UISpacerCreate(&row->e, UI_ELEMENT_H_FILL, 0, 0);
				InspectorBind(&UIButtonCreate(&row->e, UI_BUTTON_SMALL, "Delete", -1)->e, object->id, cPropertyName, INSPECTOR_DELETE_ARRAY_ITEM);
				sprintf(cPropertyName, "layers_%d_layer", i);
				InspectorAddLink(object, "Layer:", cPropertyName);
				sprintf(cPropertyName, "layers_%d_offset", i);
				InspectorAddFourGroup(object, "Offset (dpx):", cPropertyName, nullptr, true /* defaultToIndividualTab */);
				sprintf(cPropertyName, "layers_%d_position", i);
				InspectorAddFourGroup(object, "Position (%):", cPropertyName, nullptr, true /* defaultToIndividualTab */);
				sprintf(cPropertyName, "layers_%d_mode", i);
				UILabelCreate(0, 0, "Mode:", -1);
				UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
				InspectorAddRadioSwitch(object, "Background", cPropertyName, THEME_LAYER_MODE_BACKGROUND);
				InspectorAddRadioSwitch(object, "Shadow", cPropertyName, THEME_LAYER_MODE_SHADOW);
				InspectorAddRadioSwitch(object, "Content", cPropertyName, THEME_LAYER_MODE_CONTENT);
				InspectorAddRadioSwitch(object, "Overlay", cPropertyName, THEME_LAYER_MODE_OVERLAY);
				InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, cPropertyName, INSPECTOR_REMOVE_BUTTON);
				UIParentPop();
				UIParentPop();

				if (i != layerCount - 1) {
					sprintf(cPropertyName, "layers_%d_", i);
					InspectorBind(&UIButtonCreate(&UIPanelCreate(0, 0)->e, UI_BUTTON_SMALL, "Swap", -1)->e, 
							object->id, cPropertyName, INSPECTOR_SWAP_ARRAY_ITEMS);
				}
			}

			InspectorBind(&UIButtonCreate(0, 0, "Add layer", -1)->e, object->id, "layers_count", INSPECTOR_ADD_ARRAY_ITEM);
		} else if (object->type == OBJ_PAINT_OVERWRITE) {
			InspectorAddLink(object, "Color:", "color");
		} else if (object->type == OBJ_PAINT_LINEAR_GRADIENT || object->type == OBJ_PAINT_RADIAL_GRADIENT) {
			if (object->type == OBJ_PAINT_LINEAR_GRADIENT) {
				InspectorAddFloat(object, "Transform X:", "transformX");
				InspectorAddFloat(object, "Transform Y:", "transformY");
				InspectorAddFloat(object, "Transform start:", "transformStart");
			} else {
				InspectorAddFloat(object, "Transform X scale:",  "transform0");
				InspectorAddFloat(object, "Transform X offset:", "transform2");
				InspectorAddFloat(object, "Transform Y scale:",  "transform4");
				InspectorAddFloat(object, "Transform Y offset:", "transform5");
				InspectorAddFloat(object, "Transform X skew:",   "transform1");
				InspectorAddFloat(object, "Transform Y skew:",   "transform3");
			}

			UILabelCreate(0, 0, "Repeat mode:", -1);
			UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL);
			InspectorAddRadioSwitch(object, "Clamp", "repeatMode", RAST_REPEAT_CLAMP);
			InspectorAddRadioSwitch(object, "Normal", "repeatMode", RAST_REPEAT_NORMAL);
			InspectorAddRadioSwitch(object, "Mirror", "repeatMode", RAST_REPEAT_MIRROR);
			InspectorBind(&UIButtonCreate(0, UI_BUTTON_SMALL, "X", 1)->e, object->id, "repeatMode", INSPECTOR_REMOVE_BUTTON);
			UIParentPop();

			InspectorAddBooleanToggle(object, "Use gamma interpolation", "useGammaInterpolation");

			if (object->type == OBJ_PAINT_LINEAR_GRADIENT) {
				InspectorAddBooleanToggle(object, "Use window tint color", "useSystemColor");
			}

			int32_t stopCount = PropertyReadInt32(object, "stops_count");
			if (stopCount < 0) stopCount = 0;
			if (stopCount > 100) stopCount = 100;

			UILabelCreate(0, 0, "Gradient stops:", -1);

			for (int32_t i = 0; i < stopCount; i++) {
				char cPropertyName[PROPERTY_NAME_SIZE];
				UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_BORDER | UI_PANEL_MEDIUM_SPACING | UI_PANEL_EXPAND);
				sprintf(cPropertyName, "stops_%d_", i);
				UIPanel *row = UIPanelCreate(0, UI_PANEL_HORIZONTAL);
				UISpacerCreate(&row->e, UI_ELEMENT_H_FILL, 0, 0);
				InspectorBind(&UIButtonCreate(&row->e, UI_BUTTON_SMALL, "Delete", -1)->e, object->id, cPropertyName, INSPECTOR_DELETE_ARRAY_ITEM);
				sprintf(cPropertyName, "stops_%d_color", i);
				InspectorAddLink(object, "Color:", cPropertyName);
				sprintf(cPropertyName, "stops_%d_position", i);
				InspectorAddInteger(object, "Position (%):", cPropertyName);
				UIParentPop();

				if (i != stopCount - 1) {
					sprintf(cPropertyName, "stops_%d_", i);
					InspectorBind(&UIButtonCreate(&UIPanelCreate(0, 0)->e, UI_BUTTON_SMALL, "Swap", -1)->e, 
							object->id, cPropertyName, INSPECTOR_SWAP_ARRAY_ITEMS);
				}
			}

			InspectorBind(&UIButtonCreate(0, 0, "Add stop", -1)->e, object->id, "stops_count", INSPECTOR_ADD_ARRAY_ITEM);
		} else if (object->type == OBJ_MOD_COLOR) {
			InspectorAddLink(object, "Base color:", "base");
			UILabelCreate(0, 0, "Brightness (%):", -1);
			InspectorBind(&UITextboxCreate(0, UI_ELEMENT_H_FILL)->e, object->id, "brightness", INSPECTOR_INTEGER_TEXTBOX);
			UILabelCreate(0, 0, "Hue shift (deg):", -1);
			InspectorBind(&UITextboxCreate(0, UI_ELEMENT_H_FILL)->e, object->id, "hueShift", INSPECTOR_INTEGER_TEXTBOX);
		} else if (object->type == OBJ_MOD_MULTIPLY) {
			InspectorAddLink(object, "Base integer:", "base");
			UILabelCreate(0, 0, "Factor (%):", -1);
			InspectorBind(&UITextboxCreate(0, UI_ELEMENT_H_FILL)->e, object->id, "factor", INSPECTOR_INTEGER_TEXTBOX);
		}
	} else {
		UILabelCreate(0, 0, "Select an object to inspect.", -1);
	}

	UIParentPop();
	UIElementRefresh(inspector);
}

//////////////////////////////////////////////////////////////

int32_t GraphGetInteger(Object *object, int depth = 0) {
	if (!object || depth == 100) {
		return 0;
	}

	if (object->type == OBJ_VAR_INT) {
		return PropertyReadInt32(object, "value");
	} else if (object->type == OBJ_MOD_MULTIPLY) {
		Property *property = PropertyFind(object, "base", PROP_OBJECT);
		int32_t base = GraphGetInteger(ObjectFind(property ? property->object : 0), depth + 1);
		int32_t factor = PropertyReadInt32(object, "factor");
		return base * factor / 100;
	} else {
		return 0;
	}
}

int32_t GraphGetIntegerFromProperty(Property *property) {
	if (!property) {
		return 0;
	} else if (property->type == PROP_INT) {
		return property->integer;
	} else if (property->type == PROP_OBJECT) {
		return GraphGetInteger(ObjectFind(property->object));
	} else {
		return 0;
	}
}

uint32_t GraphGetColor(Object *object, int depth = 0) {
	if (!object || depth == 100) {
		return 0;
	}

	if (object->type == OBJ_VAR_COLOR) {
		return PropertyReadInt32(object, "color");
	} else if (object->type == OBJ_MOD_COLOR) {
		Property *property = PropertyFind(object, "base", PROP_OBJECT);
		uint32_t base = GraphGetColor(ObjectFind(property ? property->object : 0), depth + 1);
		uint32_t alpha = base & 0xFF000000;
		int32_t brightness = PropertyReadInt32(object, "brightness");
		int32_t hueShift = PropertyReadInt32(object, "hueShift");
		double hue, saturation, luminosity, red, green, blue;
		rgb2hsluv(UI_COLOR_RED_F(base), UI_COLOR_GREEN_F(base), UI_COLOR_BLUE_F(base), &hue, &saturation, &luminosity);
		luminosity += luminosity * brightness / 100.0f;
		hue = fmod(hue + hueShift, 360.0);
		if (luminosity < 0.0) luminosity = 0.0;
		if (luminosity > 100.0) luminosity = 100.0;
		hsluv2rgb(hue, saturation, luminosity, &red, &green, &blue);
		return UI_COLOR_FROM_FLOAT(red, green, blue) | alpha;
	} else {
		return 0;
	}
}

//////////////////////////////////////////////////////////////

void ExportGradientStopArray(Object *object, EsBuffer *data, size_t stopCount) {
	for (uintptr_t i = 0; i < stopCount; i++) {
		char cPropertyName[PROPERTY_NAME_SIZE];
		sprintf(cPropertyName, "stops_%d_color", (int32_t) i);
		Property *color = PropertyFind(object, cPropertyName, PROP_OBJECT);
		sprintf(cPropertyName, "stops_%d_position", (int32_t) i);
		Property *position = PropertyFind(object, cPropertyName, PROP_OBJECT);

		ThemeGradientStop stop = {};
		stop.color = GraphGetColor(ObjectFind(color ? color->object : 0));
		stop.position = GraphGetIntegerFromProperty(position);
		EsBufferWrite(data, &stop, sizeof(stop));
	}
}

int8_t ExportPaint(Object *object, EsBuffer *data, int depth = 0) {
	if (!object || depth == 100) {
		return 0;
	}

	if (object->type == OBJ_VAR_COLOR || object->type == OBJ_MOD_COLOR) {
		if (data) {
			ThemePaintSolid solid = {};
			solid.color = GraphGetColor(object);
			EsBufferWrite(data, &solid, sizeof(solid));
		}

		return THEME_PAINT_SOLID;
	} else if (object->type == OBJ_PAINT_OVERWRITE) {
		Property *property = PropertyFindOrInherit(false, object, "color", PROP_OBJECT);
		Object *object = ObjectFind(property ? property->object : 0);
		ExportPaint(object, data, depth + 1);
		return THEME_PAINT_OVERWRITE;
	} else if (object->type == OBJ_PAINT_LINEAR_GRADIENT) {
		if (data) {
			Property *transformX = PropertyFindOrInherit(false, object, "transformX", PROP_FLOAT);
			Property *transformY = PropertyFindOrInherit(false, object, "transformY", PROP_FLOAT);
			Property *transformStart = PropertyFindOrInherit(false, object, "transformStart", PROP_FLOAT);
			Property *stopCount = PropertyFindOrInherit(false, object, "stops_count", PROP_INT);
			Property *useGammaInterpolation = PropertyFindOrInherit(false, object, "useGammaInterpolation", PROP_INT);
			Property *useSystemColor = PropertyFindOrInherit(false, object, "useSystemColor", PROP_INT);
			Property *repeatMode = PropertyFindOrInherit(false, object, "repeatMode", PROP_INT);

			ThemePaintLinearGradient paint = {};
			paint.transform[0] = transformX ? transformX->floating : 0;
			paint.transform[1] = transformY ? transformY->floating : 0;
			paint.transform[2] = transformStart ? transformStart->floating : 0;
			paint.stopCount = stopCount ? stopCount->integer : 0;
			paint.useGammaInterpolation = useGammaInterpolation ? !!useGammaInterpolation->integer : false;
			paint.useSystemColor = useSystemColor ? !!useSystemColor->integer : false;
			paint.repeatMode = repeatMode ? repeatMode->integer : 0;
			EsBufferWrite(data, &paint, sizeof(paint));

			ExportGradientStopArray(object, data, paint.stopCount);
		}

		return THEME_PAINT_LINEAR_GRADIENT;
	} else if (object->type == OBJ_PAINT_RADIAL_GRADIENT) {
		if (data) {
			Property *transform0 = PropertyFindOrInherit(false, object, "transform0", PROP_FLOAT);
			Property *transform1 = PropertyFindOrInherit(false, object, "transform1", PROP_FLOAT);
			Property *transform2 = PropertyFindOrInherit(false, object, "transform2", PROP_FLOAT);
			Property *transform3 = PropertyFindOrInherit(false, object, "transform3", PROP_FLOAT);
			Property *transform4 = PropertyFindOrInherit(false, object, "transform4", PROP_FLOAT);
			Property *transform5 = PropertyFindOrInherit(false, object, "transform5", PROP_FLOAT);
			Property *stopCount = PropertyFindOrInherit(false, object, "stops_count", PROP_INT);
			Property *useGammaInterpolation = PropertyFindOrInherit(false, object, "useGammaInterpolation", PROP_INT);
			Property *repeatMode = PropertyFindOrInherit(false, object, "repeatMode", PROP_INT);

			ThemePaintRadialGradient paint = {};
			paint.transform[0] = transform0 ? transform0->floating : 0;
			paint.transform[1] = transform1 ? transform1->floating : 0;
			paint.transform[2] = transform2 ? transform2->floating : 0;
			paint.transform[3] = transform3 ? transform3->floating : 0;
			paint.transform[4] = transform4 ? transform4->floating : 0;
			paint.transform[5] = transform5 ? transform5->floating : 0;
			paint.stopCount = stopCount ? stopCount->integer : 0;
			paint.useGammaInterpolation = useGammaInterpolation ? !!useGammaInterpolation->integer : false;
			paint.repeatMode = repeatMode ? repeatMode->integer : 0;
			EsBufferWrite(data, &paint, sizeof(paint));

			ExportGradientStopArray(object, data, paint.stopCount);
		}

		return THEME_PAINT_RADIAL_GRADIENT;
	} else {
		return 0;
	}
}

void ExportLayerBox(bool first, Object *object, EsBuffer *data) {
	Property *mainPaint = PropertyFindOrInherit(first, object, "mainPaint", PROP_OBJECT);
	Property *borderPaint = PropertyFindOrInherit(first, object, "borderPaint", PROP_OBJECT);
	ThemeLayerBox box = {};
	box.mainPaintType = ExportPaint(ObjectFind(mainPaint ? mainPaint->object : 0), nullptr);
	box.borderPaintType = ExportPaint(ObjectFind(borderPaint ? borderPaint->object : 0), nullptr);
	box.borders.l = GraphGetIntegerFromProperty(PropertyFindOrInherit(first, object, "borders0"));
	box.borders.r = GraphGetIntegerFromProperty(PropertyFindOrInherit(first, object, "borders1"));
	box.borders.t = GraphGetIntegerFromProperty(PropertyFindOrInherit(first, object, "borders2"));
	box.borders.b = GraphGetIntegerFromProperty(PropertyFindOrInherit(first, object, "borders3"));
	box.corners.tl = GraphGetIntegerFromProperty(PropertyFindOrInherit(first, object, "corners0"));
	box.corners.tr = GraphGetIntegerFromProperty(PropertyFindOrInherit(first, object, "corners1"));
	box.corners.bl = GraphGetIntegerFromProperty(PropertyFindOrInherit(first, object, "corners2"));
	box.corners.br = GraphGetIntegerFromProperty(PropertyFindOrInherit(first, object, "corners3"));
	if (GraphGetIntegerFromProperty(PropertyFindOrInherit(first, object, "isBlurred"   ))) box.flags |= THEME_LAYER_BOX_IS_BLURRED;
	if (GraphGetIntegerFromProperty(PropertyFindOrInherit(first, object, "autoCorners" ))) box.flags |= THEME_LAYER_BOX_AUTO_CORNERS;
	if (GraphGetIntegerFromProperty(PropertyFindOrInherit(first, object, "autoBorders" ))) box.flags |= THEME_LAYER_BOX_AUTO_BORDERS;
	if (GraphGetIntegerFromProperty(PropertyFindOrInherit(first, object, "shadowHiding"))) box.flags |= THEME_LAYER_BOX_SHADOW_HIDING;
	EsBufferWrite(data, &box, sizeof(box));
	ExportPaint(ObjectFind(mainPaint ? mainPaint->object : 0), data);
	ExportPaint(ObjectFind(borderPaint ? borderPaint->object : 0), data);
}

void ExportPaintAsLayerBox(Object *object, EsBuffer *data) {
	ThemeLayerBox box = {};
	box.mainPaintType = ExportPaint(object, nullptr);
	EsBufferWrite(data, &box, sizeof(box));
	ExportPaint(object, data);
}

//////////////////////////////////////////////////////////////

#define CANVAS_ALIGN (20)

float canvasPanX, canvasPanY;
float canvasLastPanPointX, canvasLastPanPointY;
bool canvasDragging, canvasCanDrag, canvasSelecting;
int32_t canvasDragDeltaX, canvasDragDeltaY;
int32_t canvasSelectX, canvasSelectY;
int32_t canvasDragOffsetX, canvasDragOffsetY;
int32_t canvasLeftDownX, canvasLeftDownY;
bool canvasShowArrows;

UIRectangle CanvasGetObjectBounds(Object *object) {
	int32_t x = PropertyReadInt32(object, "_graphX") - canvasPanX + canvas->bounds.l;
	int32_t y = PropertyReadInt32(object, "_graphY") - canvasPanY + canvas->bounds.t;
	int32_t w = PropertyReadInt32(object, "_graphW");
	int32_t h = PropertyReadInt32(object, "_graphH");

	if (object->flags & OBJECT_IS_SELECTED && canvasDragging) {
		x += canvasDragDeltaX;
		y += canvasDragDeltaY;
	}

	return UI_RECT_4(x, x + w, y, y + h);
}

void CanvasSelectObject(Object *object) {
	for (uintptr_t i = 0; i < objects.Length(); i++) {
		objects[i].flags &= ~OBJECT_IS_SELECTED;
	}

	UIRectangle bounds = CanvasGetObjectBounds(object);
	canvasPanX += bounds.l - UI_RECT_WIDTH(canvas->bounds) / 2;
	canvasPanY += bounds.t - UI_RECT_HEIGHT(canvas->bounds) / 2;
	ObjectSetSelected(object->id);
	UIElementRepaint(canvas, nullptr);
	InspectorPopulate();
}

void CanvasDrawArrow(UIPainter *painter, int x0, int y0, int x1, int y1, uint32_t color) {
	if (!UIDrawLine(painter, x0, y0, x1, y1, color)) return;
	float angle = atan2f(y1 - y0, x1 - x0);
	UIDrawLine(painter, x0, y0, x0 + cosf(angle + 0.5f) * 15, y0 + sinf(angle + 0.5f) * 15, color);
	UIDrawLine(painter, x0, y0, x0 + cosf(angle - 0.5f) * 15, y0 + sinf(angle - 0.5f) * 15, color);
}

void CanvasDrawLayerFromData(UIPainter *painter, UIRectangle bounds, EsBuffer data) {
	EsPaintTarget paintTarget = {};
	EsPainter themePainter = {};
	themePainter.target = &paintTarget;
	themePainter.clip.l = painter->clip.l;
	themePainter.clip.r = painter->clip.r;
	themePainter.clip.t = painter->clip.t;
	themePainter.clip.b = painter->clip.b;
	themePainter.target->bits = painter->bits;
	themePainter.target->width = painter->width;
	themePainter.target->height = painter->height;
	themePainter.target->stride = painter->width * 4;

	data.bytes = data.position;
	data.position = 0;

	ThemeDrawLayer(&themePainter, bounds, &data, 1 /* TODO preview scale */, UI_RECT_1(0) /* TODO opaqueRegion */);
}

void CanvasDrawColorSwatch(Object *object, UIRectangle bounds, UIPainter *painter) {
	uint8_t buffer[4096];
	EsBuffer data = { .out = buffer, .bytes = sizeof(buffer) };
	ThemeLayer layer = { .position = { .r = 100, .b = 100 }, .type = THEME_LAYER_BOX };
	EsBufferWrite(&data, &layer, sizeof(layer));
	ExportPaintAsLayerBox(object, &data);
	CanvasDrawLayerFromData(painter, bounds, data);
}

void CanvasDrawLayer(Object *object, UIRectangle bounds, UIPainter *painter, int depth = 0) {
	if (!object || depth == 100) {
		return;
	}

	if (object->type == OBJ_LAYER_BOX) {
		uint8_t buffer[4096];
		EsBuffer data = { .out = buffer, .bytes = sizeof(buffer) };
		ThemeLayer layer = { .position = { .r = 100, .b = 100 }, .type = THEME_LAYER_BOX };
		EsBufferWrite(&data, &layer, sizeof(layer));
		ExportLayerBox(depth == 0, object, &data);
		CanvasDrawLayerFromData(painter, bounds, data);
	} else if (object->type == OBJ_LAYER_GROUP) {
		int32_t layerCount = PropertyReadInt32(object, "layers_count");
		if (layerCount < 0) layerCount = 0;
		if (layerCount > 100) layerCount = 100;

		int32_t inWidth = UI_RECT_WIDTH(bounds);
		int32_t inHeight = UI_RECT_HEIGHT(bounds);

		for (int32_t i = 0; i < layerCount; i++) {
			char cPropertyName[PROPERTY_NAME_SIZE];
			sprintf(cPropertyName, "layers_%d_layer", i);
			Property *layerProperty = PropertyFind(object, cPropertyName, PROP_OBJECT);
			Object *layerObject = ObjectFind(layerProperty ? layerProperty->object : 0);

			sprintf(cPropertyName, "layers_%d_offset0", i);
			int32_t offset0 = PropertyReadInt32(object, cPropertyName);
			sprintf(cPropertyName, "layers_%d_offset1", i);
			int32_t offset1 = PropertyReadInt32(object, cPropertyName);
			sprintf(cPropertyName, "layers_%d_offset2", i);
			int32_t offset2 = PropertyReadInt32(object, cPropertyName);
			sprintf(cPropertyName, "layers_%d_offset3", i);
			int32_t offset3 = PropertyReadInt32(object, cPropertyName);
			sprintf(cPropertyName, "layers_%d_position0", i);
			int32_t position0 = PropertyReadInt32(object, cPropertyName);
			sprintf(cPropertyName, "layers_%d_position1", i);
			int32_t position1 = PropertyReadInt32(object, cPropertyName);
			sprintf(cPropertyName, "layers_%d_position2", i);
			int32_t position2 = PropertyReadInt32(object, cPropertyName);
			sprintf(cPropertyName, "layers_%d_position3", i);
			int32_t position3 = PropertyReadInt32(object, cPropertyName);

			UIRectangle outBounds;
			outBounds.l = bounds.l + offset0 + position0 * inWidth  / 100;
			outBounds.r = bounds.l + offset1 + position1 * inWidth  / 100;
			outBounds.t = bounds.t + offset2 + position2 * inHeight / 100;
			outBounds.b = bounds.t + offset3 + position3 * inHeight / 100;

			CanvasDrawLayer(layerObject, outBounds, painter, depth + 1);
		}
	}
}

int CanvasMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		UIDrawBlock(painter, element->bounds, 0xFFC0C0C0);
		UIRectangle selectionBounds = UI_RECT_4(MinimumInteger(canvasLeftDownX, canvasSelectX), MaximumInteger(canvasLeftDownX, canvasSelectX),
				MinimumInteger(canvasLeftDownY, canvasSelectY), MaximumInteger(canvasLeftDownY, canvasSelectY));

		if (canvasSelecting) {
			UIDrawBlock(painter, selectionBounds, 0xFF99CCFF);
		}

		for (uintptr_t i = 0; i < objects.Length(); i++) {
			Object *object = &objects[i];
			UIRectangle bounds = CanvasGetObjectBounds(object);

			if (bounds.r < element->bounds.l || bounds.l > element->bounds.r
					|| bounds.b < element->bounds.t || bounds.t > element->bounds.b) {
				continue;
			}

			UIRectangle selectionIntersection = UIRectangleIntersection(bounds, selectionBounds);

			if ((object->flags & OBJECT_IS_SELECTED) == (inspectorPickData == nullptr)
					|| (canvasSelecting && UI_RECT_VALID(selectionIntersection))) {
				UIDrawBorder(painter, UIRectangleAdd(bounds, UI_RECT_1I(-3)), 0xFF4092FF, UI_RECT_1(3));
			} 

			UIDrawString(painter, UI_RECT_4(bounds.l, element->bounds.r, bounds.t - ui.glyphHeight, bounds.t), 
					object->cName, -1, 0xFF000000, UI_ALIGN_LEFT, nullptr);

			UIDrawRectangle(painter, bounds, 0xFFE0E0E0, 0xFF404040, UI_RECT_1(1));
			UIDrawBlock(painter, UI_RECT_4(bounds.l + 1, bounds.r + 1, bounds.b, bounds.b + 1), 0xFF404040);
			UIDrawBlock(painter, UI_RECT_4(bounds.r, bounds.r + 1, bounds.t + 1, bounds.b + 1), 0xFF404040);

			if (ObjectIsConditional(object)) {
				UIRectangle indicator = UI_RECT_4(bounds.l - ui.glyphWidth, bounds.l, bounds.t, bounds.t + ui.glyphHeight);
				UIDrawBlock(painter, indicator, 0xFFFFFF00);
				UIDrawString(painter, indicator, "?", -1, 0xFF000000, UI_ALIGN_CENTER, nullptr);
			}

			bounds = UIRectangleAdd(bounds, UI_RECT_1I(3));

			if (object->type == OBJ_VAR_INT || object->type == OBJ_MOD_MULTIPLY) {
				int32_t value = GraphGetInteger(object);
				char buffer[32];
				snprintf(buffer, sizeof(buffer), "%d", value);
				UIDrawString(painter, bounds, buffer, -1, 0xFF000000, UI_ALIGN_CENTER, nullptr);
			} else if (object->type == OBJ_LAYER_BOX || object->type == OBJ_LAYER_GROUP) {
				CanvasDrawLayer(object, bounds, painter);
			} else if (object->type == OBJ_PAINT_LINEAR_GRADIENT || object->type == OBJ_PAINT_RADIAL_GRADIENT) {
				CanvasDrawColorSwatch(object, bounds, painter);
			} else if (object->type == OBJ_VAR_COLOR || object->type == OBJ_MOD_COLOR) {
				CanvasDrawColorSwatch(object, bounds, painter);
				uint32_t color = GraphGetColor(object);
				bool isLight = EsColorIsLight(color);
				char buffer[32];
				snprintf(buffer, sizeof(buffer), "%.8X", color);
				UIRectangle area = UI_RECT_4(bounds.l, bounds.r, bounds.t, bounds.t + UIMeasureStringHeight());
				UIDrawString(painter, area, buffer, -1, isLight ? 0xFF000000 : 0xFFFFFFFF, UI_ALIGN_CENTER, nullptr);
			} else {
				// TODO Preview for more object types: style, text style, icon style, metrics layer, text layer.
			}
		}

		if (canvasShowArrows) {
			// Draw object connections.
			// TODO This will be awfully slow when there's many objects...

			for (uintptr_t i = 0; i < objects.Length(); i++) {
				Object *object = &objects[i];
				UIRectangle b1 = CanvasGetObjectBounds(object);

				for (uintptr_t j = 0; j < object->properties.Length(); j++) {
					if (object->properties[j].type == PROP_OBJECT) {
						Object *target = ObjectFind(object->properties[j].object);
						if (!target) continue;
						UIRectangle b2 = CanvasGetObjectBounds(target);
						CanvasDrawArrow(painter, (b2.l + b2.r) / 2, (b2.t + b2.b) / 2, (b1.l + b1.r) / 2, (b1.t + b1.b) / 2, 0xFF000000);
					}
				}
			}
		}
	} else if (message == UI_MSG_LEFT_DOWN) {
		canvasCanDrag = true;
		bool foundObjectToSelect = false;

		for (uintptr_t i = objects.Length(); i > 0; i--) {
			Object *object = &objects[i - 1];
			UIRectangle bounds = CanvasGetObjectBounds(object);

			if (UIRectangleContains(bounds, element->window->cursorX, element->window->cursorY)) {
				if (inspectorPickData) {
					canvasCanDrag = false;

					Step step = {};
					step.type = STEP_MODIFY_PROPERTY;
					step.objectID = inspectorPickData->objectID;
					strcpy(step.property.cName, inspectorPickData->cPropertyName);
					step.property.type = PROP_OBJECT;
					step.property.object = object->id;
					InspectorBroadcastStep(step, inspectorPickData);
					DocumentApplyStep(step);
				} else {
					if (~object->flags & OBJECT_IS_SELECTED) {
						for (uintptr_t i = 0; i < objects.Length(); i++) {
							objects[i].flags &= ~OBJECT_IS_SELECTED;
						}
					}

					ObjectSetSelected(object->id, false /* do not clear selection flag from previous */);
					canvasDragOffsetX = bounds.l - element->window->cursorX;
					canvasDragOffsetY = bounds.t - element->window->cursorY;
				}

				foundObjectToSelect = true;
				break;
			}
		}

		if (!foundObjectToSelect) {
			ObjectSetSelected(0);

			for (uintptr_t i = 0; i < objects.Length(); i++) {
				objects[i].flags &= ~OBJECT_IS_SELECTED;
			}
		}

		canvasLeftDownX = element->window->cursorX;
		canvasLeftDownY = element->window->cursorY;

		UIElementRepaint(element, nullptr);
		InspectorPopulate();
		InspectorPickTargetEnd();
	} else if (message == UI_MSG_LEFT_UP && canvasDragging) {
		Object *object = ObjectFind(selectedObjectID);
		int32_t oldX = PropertyReadInt32(object, "_graphX");
		int32_t oldY = PropertyReadInt32(object, "_graphY");

		if ((canvasDragDeltaX || canvasDragDeltaY) && object) {
			Step step = {};
			step.type = STEP_MODIFY_PROPERTY;
			step.property.type = PROP_INT;

			for (uintptr_t i = 0; i < objects.Length(); i++) {
				Object *object = &objects[i];

				if ((object->flags & OBJECT_IS_SELECTED) && object->id != selectedObjectID) {
					step.objectID = object->id;
					strcpy(step.property.cName, "_graphX");
					step.property.integer = PropertyReadInt32(object, "_graphX") + canvasDragDeltaX;
					DocumentApplyStep(step, STEP_APPLY_GROUPED);
					strcpy(step.property.cName, "_graphY");
					step.property.integer = PropertyReadInt32(object, "_graphY") + canvasDragDeltaY;
					DocumentApplyStep(step, STEP_APPLY_GROUPED);
				}
			}

			step.objectID = selectedObjectID;
			strcpy(step.property.cName, "_graphX");
			step.property.integer = oldX + canvasDragDeltaX;
			DocumentApplyStep(step, STEP_APPLY_GROUPED);
			strcpy(step.property.cName, "_graphY");
			step.property.integer = oldY + canvasDragDeltaY;
			DocumentApplyStep(step);
		}

		canvasDragging = false;
		UIElementRepaint(element, nullptr);
	} else if (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 1 && selectedObjectID && canvasCanDrag) {
		int32_t dx = canvasLeftDownX - element->window->cursorX;
		int32_t dy = canvasLeftDownY - element->window->cursorY;

		if (canvasDragging || dx * dx + dy * dy > 200) {
			int32_t canvasDragNewX = element->window->cursorX + canvasPanX + canvasDragOffsetX - element->bounds.l;
			int32_t canvasDragNewY = element->window->cursorY + canvasPanY + canvasDragOffsetY - element->bounds.t;
			canvasDragNewX -= canvasDragNewX % CANVAS_ALIGN, canvasDragNewY -= canvasDragNewY % CANVAS_ALIGN;
			canvasDragDeltaX = canvasDragNewX - PropertyReadInt32(ObjectFind(selectedObjectID), "_graphX");
			canvasDragDeltaY = canvasDragNewY - PropertyReadInt32(ObjectFind(selectedObjectID), "_graphY");
			canvasDragging = true;
			UIElementRepaint(element, nullptr);
		}
	} else if (message == UI_MSG_LEFT_UP && canvasSelecting) {
		UIRectangle selectionBounds = UI_RECT_4(MinimumInteger(canvasLeftDownX, canvasSelectX), MaximumInteger(canvasLeftDownX, canvasSelectX),
				MinimumInteger(canvasLeftDownY, canvasSelectY), MaximumInteger(canvasLeftDownY, canvasSelectY));

		for (uintptr_t i = 0; i < objects.Length(); i++) {
			Object *object = &objects[i];
			UIRectangle bounds = CanvasGetObjectBounds(object);
			UIRectangle selectionIntersection = UIRectangleIntersection(bounds, selectionBounds);

			if (UI_RECT_VALID(selectionIntersection)) {
				object->flags |= OBJECT_IS_SELECTED;
			}
		}

		canvasSelecting = false;
		UIElementRepaint(element, nullptr);
	} else if (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 1 && !selectedObjectID) {
		canvasSelectX = element->window->cursorX;
		canvasSelectY = element->window->cursorY;
		canvasSelecting = true;
		UIElementRepaint(element, nullptr);
	} else if (message == UI_MSG_MIDDLE_DOWN) {
		canvasLastPanPointX = element->window->cursorX;
		canvasLastPanPointY = element->window->cursorY;
		_UIWindowSetCursor(element->window, UI_CURSOR_HAND);
	} else if (message == UI_MSG_MIDDLE_UP) {
		_UIWindowSetCursor(element->window, UI_CURSOR_ARROW);
	} else if (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 2) {
		canvasPanX -= element->window->cursorX - canvasLastPanPointX;
		canvasPanY -= element->window->cursorY - canvasLastPanPointY;
		canvasLastPanPointX = element->window->cursorX;
		canvasLastPanPointY = element->window->cursorY;
		UIElementRepaint(element, nullptr);
	} else if (message == UI_MSG_LAYOUT) {
		int width = UIElementMessage(canvasControls, UI_MSG_GET_WIDTH, 0, 0);
		int height = UIElementMessage(canvasControls, UI_MSG_GET_HEIGHT, 0, 0);
		UIRectangle bounds = UI_RECT_4(element->bounds.l + 10, element->bounds.l + 10 + width, element->bounds.b - 10 - height, element->bounds.b - 10);
		UIElementMove(canvasControls, bounds, false);
	}

	return 0;
}

void CanvasToggleArrows(void *) {
	canvasShowArrows = !canvasShowArrows;
	UIElementRepaint(canvas, nullptr);
}

//////////////////////////////////////////////////////////////

void ObjectAddCommandInternal(void *cp) {
	Object object = {};
	object.type = (ObjectType) (uintptr_t) cp;
	object.id = ++objectIDAllocator;
	Property p;
	int32_t x = canvasPanX + UI_RECT_WIDTH(canvas->bounds) / 2;
	int32_t y = canvasPanY + UI_RECT_HEIGHT(canvas->bounds) / 2;
	x -= x % CANVAS_ALIGN, y -= y % CANVAS_ALIGN;
	int32_t w = object.type == OBJ_COMMENT ? 30 : 80;
	int32_t h = object.type == OBJ_COMMENT ? 10 : 60;
	p = { .type = PROP_INT, .integer = x }; strcpy(p.cName, "_graphX"); object.properties.Add(p);
	p = { .type = PROP_INT, .integer = y }; strcpy(p.cName, "_graphY"); object.properties.Add(p);
	p = { .type = PROP_INT, .integer = w }; strcpy(p.cName, "_graphW"); object.properties.Add(p);
	p = { .type = PROP_INT, .integer = h }; strcpy(p.cName, "_graphH"); object.properties.Add(p);
	
	Step step = {};
	step.type = STEP_ADD_OBJECT;
	step.object = object;
	DocumentApplyStep(step);

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		objects[i].flags &= ~OBJECT_IS_SELECTED;
	}

	ObjectSetSelected(object.id);
	InspectorPopulate();
}

void ObjectAddCommand(void *) {
	UIMenu *menu = UIMenuCreate(window->pressed, UI_MENU_NO_SCROLL);
	UIMenuAddItem(menu, 0, "Style", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_STYLE);
	UIMenuAddItem(menu, 0, "Comment", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_COMMENT);
	UIMenuAddItem(menu, 0, "Color variable", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_VAR_COLOR);
	UIMenuAddItem(menu, 0, "Integer variable", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_VAR_INT);
	UIMenuAddItem(menu, 0, "Text style", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_VAR_TEXT_STYLE);
	UIMenuAddItem(menu, 0, "Icon style", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_VAR_ICON_STYLE);
	UIMenuAddItem(menu, 0, "Metrics", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_LAYER_METRICS);
	UIMenuAddItem(menu, 0, "Overwrite paint", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_PAINT_OVERWRITE);
	UIMenuAddItem(menu, 0, "Linear gradient", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_PAINT_LINEAR_GRADIENT);
	UIMenuAddItem(menu, 0, "Radial gradient", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_PAINT_RADIAL_GRADIENT);
	UIMenuAddItem(menu, 0, "Box layer", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_LAYER_BOX);
	UIMenuAddItem(menu, 0, "Text layer", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_LAYER_TEXT);
	UIMenuAddItem(menu, 0, "Layer group", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_LAYER_GROUP);
	UIMenuAddItem(menu, 0, "Modify color", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_MOD_COLOR);
	UIMenuAddItem(menu, 0, "Modify integer", -1, ObjectAddCommandInternal, (void *) (uintptr_t) OBJ_MOD_MULTIPLY);
	UIMenuShow(menu);
}

void ObjectDeleteCommand(void *) {
	Array<uint64_t> list = {};

	for (uintptr_t i = 0; i < objects.Length(); i++) {
		Object *object = &objects[i];

		if (object->flags & OBJECT_IS_SELECTED) {
			list.Add(object->id);
		}
	}

	Step step = {};
	step.type = STEP_DELETE_OBJECT;

	for (uintptr_t i = 0; i < list.Length(); i++) {
		step.objectID = list[i];
		DocumentApplyStep(step, i == list.Length() - 1 ? STEP_APPLY_NORMAL : STEP_APPLY_GROUPED);
	}

	list.Free();
}

void ObjectDuplicateCommand(void *) {
	if (!selectedObjectID) return;

	Object *source = ObjectFind(selectedObjectID);
	UI_ASSERT(source);

	Object object = {};
	object.type = source->type;
	object.id = ++objectIDAllocator;
	object.properties.InsertMany(0, source->properties.Length());
	memcpy(object.properties.array, source->properties.array, source->properties.Length() * sizeof(Property));

	Property *graphX = PropertyFind(&object, "_graphX", PROP_INT);
	if (graphX) graphX->integer += 60;
	
	Step step = {};
	step.type = STEP_ADD_OBJECT;
	step.object = object;
	DocumentApplyStep(step);
}

//////////////////////////////////////////////////////////////

int WindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_WINDOW_CLOSE) {
#ifndef OS_ESSENCE
		if (documentModified && !window->dialog) {
			const char *dialog = "Document modified. Save changes?\n%f%b%b";
			const char *result = UIDialogShow(window, 0, dialog, "Save", "Discard");

			if (0 == strcmp(result, "Save")) {
				DocumentSave(nullptr);
			}
		}
#endif
	}

	return 0;
}

#ifdef OS_ESSENCE
void DocumentFileMenu(void *) {
	EsFileMenuCreate(ui.instance, ui.instance->window, ES_MENU_AT_CURSOR);
}
#endif

int main() {
	UIInitialise();
	ui.theme = _uiThemeClassic;
	window = UIWindowCreate(0, UI_ELEMENT_PARENT_PUSH | UI_WINDOW_MAXIMIZE, "Designer", 0, 0);
	window->e.messageUser = WindowMessage;
	UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_EXPAND);

	UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_HORIZONTAL | UI_PANEL_GRAY | UI_PANEL_SMALL_SPACING);
#ifdef OS_ESSENCE
		UIButtonCreate(0, UI_BUTTON_DROP_DOWN, "File", -1)->invoke = DocumentFileMenu;
#else
		UIButtonCreate(0, 0, "Save", -1)->invoke = DocumentSave;
#endif
		UIButtonCreate(0, 0, "Add object...", -1)->invoke = ObjectAddCommand;
		UISpacerCreate(0, 0, 15, 0);
		labelMessage = UILabelCreate(0, UI_ELEMENT_H_FILL, 0, 0);
	UIParentPop();

	UISpacerCreate(0, UI_SPACER_LINE, 0, 1);

	UISplitPaneCreate(0, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_V_FILL, 0.75f);
	canvas = UIElementCreate(sizeof(UIElement), 0, 0, CanvasMessage, "Canvas");
	inspector = &UIPanelCreate(0, UI_PANEL_GRAY | UI_PANEL_MEDIUM_SPACING | UI_PANEL_SCROLL | UI_PANEL_EXPAND)->e;
	InspectorPopulate();

	canvasControls = &UIPanelCreate(canvas, UI_PANEL_HORIZONTAL | UI_ELEMENT_PARENT_PUSH)->e;
		UIButtonCreate(0, 0, "Toggle arrows", -1)->invoke = CanvasToggleArrows;
	UIParentPop();

	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_LETTER('Z'), 1 /* ctrl */, 0, 0, DocumentUndoStep, 0));
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_LETTER('Y'), 1 /* ctrl */, 0, 0, DocumentRedoStep, 0));
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_LETTER('D'), 1 /* ctrl */, 0, 0, ObjectDuplicateCommand, 0));
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_DELETE, 0, 0, 0, ObjectDeleteCommand, 0));

#ifdef OS_ESSENCE
	EsWindowSetIcon(ui.instance->window, ES_ICON_APPLICATIONS_INTERFACEDESIGN);
	EsInstanceSetClassEditor(ui.instance, &instanceClassEditorSettings);
#else
	UIWindowRegisterShortcut(window, UI_SHORTCUT(UI_KEYCODE_LETTER('S'), 1 /* ctrl */, 0, 0, DocumentSave, 0));
	DocumentLoad();
#endif

	int result = UIMessageLoop();
	DocumentFree();
	UIElementDestroy(&window->e);
	_UIUpdate();
	inspectorBoundElements.Free();
	return result;
}

#ifdef OS_ESSENCE
void _UIMessageProcess(EsMessage *message) {
	if (message->type == ES_MSG_INSTANCE_OPEN) {
		DocumentFree();
		fileStore = message->instanceOpen.file;
		DocumentLoad();
		fileStore = nullptr;
		EsInstanceOpenComplete(message, true);
	} else if (message->type == ES_MSG_INSTANCE_SAVE) {
		fileStore = message->instanceSave.file;
		DocumentSave(nullptr);
		fileStore = nullptr;
		EsInstanceSaveComplete(message, true);
	}
}

void _start() {
	_init();
	main();
}
#endif