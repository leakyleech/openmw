
#include "refcollection.hpp"

#include <sstream>
#include <iostream>

#include <components/misc/stringops.hpp>
#include <components/esm/loadcell.hpp>

#include "ref.hpp"
#include "cell.hpp"
#include "universalid.hpp"
#include "record.hpp"

void CSMWorld::RefCollection::load (ESM::ESMReader& reader, int cellIndex, bool base,
    std::map<std::string, std::map<ESM::RefNum, std::string> >& cache, const std::string& origCellId,
    CSMDoc::Messages& messages)
{
    std::string cellid = origCellId;
    Record<Cell> cell = mCells.getRecord (cellIndex);

    Cell& cell2 = base ? cell.mBase : cell.mModified;

    CellRef ref;

    bool deleted = false;
    ESM::MovedCellRef mref;

    // hack to initialise mindex
    while (!(mref.mRefNum.mIndex = 0) && ESM::Cell::getNextRef(reader, ref, deleted, true, &mref))
    {
        // Keep mOriginalCell empty when in modified (as an indicator that the
        // original cell will always be equal the current cell).
        ref.mOriginalCell = base ? cell2.mId : "";

        if (cell.get().isExterior())
        {
            // ignoring moved references sub-record; instead calculate cell from coordinates
            std::pair<int, int> index = ref.getCellIndex();

            std::ostringstream stream;
            stream << "#" << index.first << " " << index.second;

            ref.mCell = stream.str();

            // It is not always possibe to ignore moved references sub-record and calculate from
            // coordinates. Some mods may place the ref in positions outside normal bounds,
            // resulting in non sensical cell id's.
            //
            // Use the target cell from the MVRF tag but if different output an error message
            if (!base &&                  // don't try to update base records
                mref.mRefNum.mIndex != 0) // MVRF tag found
            {
                std::ostringstream stream;
                stream << "#" << mref.mTarget[0] << " " << mref.mTarget[1];
                ref.mCell = stream.str(); // overwrite

                ref.mOriginalCell = cell2.mId;

                if (deleted)
                {
                    // FIXME: how to mark the record deleted?
                    CSMWorld::UniversalId id (CSMWorld::UniversalId::Type_Cell,
                        mCells.getId (cellIndex));

                    messages.add (id, "Moved reference "+ref.mRefID+" is in DELE state");

                    continue;
                }
                else
                {
                    if (index.first != mref.mTarget[0] || index.second != mref.mTarget[1])
                    {
                        std::cerr << "The Position of moved ref "
                            << ref.mRefID << " does not match the target cell" << std::endl;
                        std::cerr << "Position: #" << index.first << " " << index.second
                            <<", Target #"<< mref.mTarget[0] << " " << mref.mTarget[1] << std::endl;
                    }

                    // transfer the ref to the new cell
                    cellid = ref.mCell;
                }
            }
        }
        else
            ref.mCell = cell2.mId;

        std::map<ESM::RefNum, std::string>::iterator iter = cache[cellid].find (ref.mRefNum);

        if (deleted)
        {
            if (iter==cache[cellid].end())
            {
                CSMWorld::UniversalId id (CSMWorld::UniversalId::Type_Cell,
                    mCells.getId (cellIndex));

                messages.add (id, "Attempt to delete a non-existing reference");

                continue;
            }

            int index = getIndex (iter->second);

            Record<CellRef> record = getRecord (index);

            if (record.mState==RecordBase::State_BaseOnly)
            {
                removeRows (index, 1);
                cache[cellid].erase (iter);
            }
            else
            {
                record.mState = RecordBase::State_Deleted;
                setRecord (index, record);
            }

            continue;
        }

        if (iter==cache[cellid].end())
        {
            // new reference
            ref.mId = getNewId();

            Record<CellRef> record;
            record.mState = base ? RecordBase::State_BaseOnly : RecordBase::State_ModifiedOnly;
            (base ? record.mBase : record.mModified) = ref;

            appendRecord (record);

            cache[cellid].insert (std::make_pair (ref.mRefNum, ref.mId));
        }
        else
        {
            // old reference -> merge
            ref.mId = iter->second;

            int index = getIndex (ref.mId);

            Record<CellRef> record = getRecord (index);
            record.mState = base ? RecordBase::State_BaseOnly : RecordBase::State_Modified;
            (base ? record.mBase : record.mModified) = ref;

            setRecord (index, record);
        }
    }
}

std::string CSMWorld::RefCollection::getNewId()
{
    std::ostringstream stream;
    stream << "ref#" << mNextId++;
    return stream.str();
}
