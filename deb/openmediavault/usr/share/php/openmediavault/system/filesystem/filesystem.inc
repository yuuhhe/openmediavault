<?php
/**
 * This file is part of OpenMediaVault.
 *
 * @license   http://www.gnu.org/licenses/gpl.html GPL Version 3
 * @author    Volker Theile <volker.theile@openmediavault.org>
 * @copyright Copyright (c) 2009-2019 Volker Theile
 *
 * OpenMediaVault is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * OpenMediaVault is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenMediaVault. If not, see <http://www.gnu.org/licenses/>.
 */
namespace OMV\System\Filesystem;

require_once("openmediavault/functions.inc");

/**
 * Class to get detailed information about a filesystem.
 * @ingroup api
 */
class Filesystem extends \OMV\System\BlockDevice
		implements FilesystemInterface, SharedFolderCandidateInterface {
	protected $uuid = "";
	protected $type = "";
	protected $label = "";
	protected $partEntry = NULL;
	protected $usage = "";
	private $dataCached = FALSE;
	private $backend = NULL;

	/**
	 * Constructor
	 * @param id The UUID or device path of the filesystem, e.g.
	 *   <ul>
	 *   \li 78b669c1-9183-4ca3-a32c-80a4e2c61e2d (EXT2/3/4, JFS, XFS)
	 *   \li 7A48-BA97 (FAT)
	 *   \li 2ED43920D438EC29 (NTFS)
	 *   \li /dev/sde1
	 *   \li /dev/disk/by-id/scsi-SATA_ST3200XXXX2AS_5XWXXXR6-part1
	 *   \li /dev/disk/by-label/DATA
	 *   \li /dev/disk/by-path/pci-0000:00:10.0-scsi-0:0:0:0-part2
	 *   \li /dev/disk/by-uuid/ad3ee177-777c-4ad3-8353-9562f85c0895
	 *   \li /dev/cciss/c0d0p2
	 *   \li /dev/disk/by-id/md-name-vmpc01:data
	 *   \li /dev/disk/by-id/md-uuid-75de9de9:6beca92e:8442575c:73eabbc9
	 *   </ul>
	 */
	public function __construct($id) {
		parent::__construct($id);
		if (TRUE === is_fs_uuid($id))
			$this->uuid = $id;
	}

	protected function isCached() {
		return $this->dataCached;
	}

	protected function setCached($cached) {
		return $this->dataCached = $cached;
	}

	/**
	 * Get the filesystem detailed information.
	 * @private
	 * @return void
	 * @throw \OMV\ExecException
	 */
	protected function getData() {
		if (FALSE !== $this->isCached())
			return;

		if (TRUE === is_fs_uuid($this->uuid)) {
			// Get the device name containing the file system. This is
			// required for the blkid low-level probing mode.
			// Do not use /dev/disk/by-uuid/xxx because this will cause
			// some problems:
			// - After creating an RAID the filesystem is not listed in
			//   /dev/disk/by-uuid.
			// - After a reboot a LVM2 logical volume is listed in
			//   /dev/disk/by-uuid but pointing to /dev/dm-X and not to
			//   /dev/mapper which is preferred.
			$cmdArgs = [];
			$cmdArgs[] = sprintf("UUID=%s", escapeshellarg($this->uuid));
			$cmd = new \OMV\System\Process("findfs", $cmdArgs);
			$cmd->setRedirect2to1();
			$cmd->execute($output);
			$this->deviceFile = $output[0];
			unset($cmdArgs, $output);
		}

		// If the device file looks like /dev/disk/by-(id|label|path|uuid)/*
		// then it is necessary to get the /dev/xxx equivalent.
		// Note, limit this to the given types, otherwise unexpected behaviors
		// might occur, e.g. the name of the LVM device /dev/mapper/vg0-lv0
		// will be replaced by /dev/dm-0.
		if (TRUE === is_devicefile_by($this->deviceFile))
			$this->deviceFile = realpath($this->deviceFile);

		// Get the file system information.
		$cmdArgs = [];
		$cmdArgs[] = "-p";
		$cmdArgs[] = "-o full";
		$cmdArgs[] = escapeshellarg($this->deviceFile);
		$cmd = new \OMV\System\Process("blkid", $cmdArgs);
		$cmd->setRedirect2to1();
		$cmd->execute($output);

		// Parse command output:
		// /dev/sda1: UUID="3d9be906-d592-449b-93c4-3dd8888801b2" TYPE="ext3"
		// /dev/sdg1: LABEL="test" UUID="d19bcea0-a323-4aea-9791-c6578180c129" TYPE="jfs"
		// /dev/sda1: UUID="128917b7-718a-4361-ab6f-6b1725564cb8" VERSION="1.0" TYPE="ext4" USAGE="filesystem" PART_ENTRY_SCHEME="dos" PART_ENTRY_TYPE="0x83" PART_ENTRY_FLAGS="0x80" PART_ENTRY_NUMBER="1" PART_ENTRY_OFFSET="2048" PART_ENTRY_SIZE="30074880" PART_ENTRY_DISK="8:0"
		// /dev/sdc1: LABEL="xfs" UUID="83d116e4-8b95-4e49-86e4-369615079b33" TYPE="xfs" USAGE="filesystem" PART_ENTRY_SCHEME="gpt" PART_ENTRY_UUID="8856f019-67da-4813-ab75-9fc3c3fe056f" PART_ENTRY_TYPE="0fc63daf-8483-4772-8e79-3d69d8477de4" PART_ENTRY_NUMBER="1" PART_ENTRY_OFFSET="2048" PART_ENTRY_SIZE="206815" PART_ENTRY_DISK="8:32"
		// /dev/sdg1: LABEL="Traveller" UUID="2218-DF1C" VERSION="FAT32" TYPE="vfat" USAGE="filesystem" PART_ENTRY_SCHEME="dos" PART_ENTRY_TYPE="0xc" PART_ENTRY_FLAGS="0x80" PART_ENTRY_NUMBER="1" PART_ENTRY_OFFSET="62" PART_ENTRY_SIZE="15635532" PART_ENTRY_DISK="8:96"
		$regex = '/^(\S+): (.+)$/i';
		if (1 !== preg_match($regex, $output[0], $matches))
			return FALSE;
		// Set default values and extract key/value pairs.
		$data = [
			"devicefile" => $matches[1],
			"uuid" => "",
			"label" => "",
			"type" => "",
			"part_entry_scheme" => "",
			"part_entry_uuid" => "",
			"part_entry_type" => "",
			"part_entry_flags" => "",
			"part_entry_number" => "",
			"part_entry_offset" => "",
			"part_entry_size" => "",
			"part_entry_disk" => "",
			"usage" => ""
		];
		$parts = preg_split("/(\S+=\\\"[^\\\"]+\\\")|[\s]+/", $matches[2],
		  -1, PREG_SPLIT_NO_EMPTY | PREG_SPLIT_DELIM_CAPTURE);
		foreach ($parts as $partk => $partv) {
			$keyValue = explode("=", $partv);
			if (count($keyValue) != 2)
				continue;
			$data[mb_strtolower($keyValue[0])] = substr($keyValue[1], 1, -1);
		}

		$this->deviceFile = $data['devicefile'];
		$this->uuid = $data['uuid'];
		$this->label = $data['label'];
		$this->type = $data['type'];
		$this->usage = $data['usage'];
		$this->partEntry = [
			"scheme" => $data['part_entry_scheme'],
			"uuid" => $data['part_entry_uuid'],
			"type" => $data['part_entry_type'],
			"flags" => $data['part_entry_flags'],
			"number" => $data['part_entry_number'],
			"offset" => $data['part_entry_offset'],
			"size" => $data['part_entry_size'],
			"disk" => $data['part_entry_disk']
		];

		// Set flag to mark information has been successfully read.
		$this->setCached(TRUE);
	}

	/**
	 * Refresh the cached information.
	 * @return void
	 */
	public function refresh() {
		$this->setCached(FALSE);
		$this->getData();
	}

	/**
	 * Checks if the filesystem exists.
	 * @return TRUE if the device exists, otherwise FALSE.
	 */
	public function exists() {
		try {
			$this->getData();
		} catch(\Exception $e) {
			return FALSE;
		}
		return TRUE;
	}

	/**
	 * Check if the filesystem has an UUID, e.g. <ul>
	 * \li 78b669c1-9183-4ca3-a32c-80a4e2c61e2d (EXT2/3/4, JFS, XFS)
	 * \li 7A48-BA97 (FAT)
	 * \li 2ED43920D438EC29 (NTFS)
	 * </ul>
	 * @see http://wiki.ubuntuusers.de/UUID
	 * @return Returns TRUE if the filesystem has an UUID, otherwise FALSE.
	 */
	public function hasUuid() {
		$uuid = $this->getUuid();
		return is_fs_uuid($uuid);
	}

	/**
	 * Assert that the filesystem has an UUID.
	 * @return void
	 * @throw \OMV\AssertException
	 */
	public function assertUuid() {
		if (FALSE === $this->hasUuid()) {
			throw new \OMV\AssertException(
			  "Filesystem '%s' does not have an UUID.",
			  $this->getDeviceFile());
		}
	}

	/**
	 * Get the UUID of the filesystem.
	 * @see http://wiki.ubuntuusers.de/UUID
	 * @return Returns the UUID of the filesystem, otherwise an empty
	 *   string.
	 */
	public function getUuid() {
		$this->getData();
		return $this->uuid;
	}

	/**
	 * Check if the filesystem has a label.
	 * @return Returns TRUE if the filesystem has a label, otherwise FALSE.
	 */
	public function hasLabel() {
		$label = $this->getLabel();
		return !empty($label);
	}

	/**
	 * Assert that the filesystem has a label.
	 * @return void
	 * @throw \OMV\AssertException
	 */
	public function assertLabel() {
		if (FALSE === $this->hasLabel()) {
			throw new \OMV\AssertException(
			  "Filesystem '%s' does not have a label.",
			  $this->getDeviceFile());
		}
	}

	/**
	 * Get the filesystem label.
	 * @return Returns the label of the filesystem, otherwise an empty
	 *   string.
	 */
	public function getLabel() {
		$this->getData();
		return $this->label;
	}

	/**
	 * Get the filesystem type, e.g. 'ext3' or 'vfat'.
	 * @return The filesystem type.
	 */
	public function getType() {
		$this->getData();
		return $this->type;
	}

	/**
	 * Get the partition scheme, e.g. 'gpt', 'mbr', 'apm' or 'dos'.
	 * @deprecated
	 * @return The filesystem type, otherwise NULL.
	 */
	public function getPartitionType() {
		return $this->getPartitionScheme();
	}

	/**
	 * Get the partition scheme, e.g. 'gpt', 'mbr', 'apm' or 'dos'.
	 * @return The filesystem type, otherwise NULL.
	 */
	public function getPartitionScheme() {
		$this->getData();
		return $this->partEntry['scheme'];
	}

	/**
	 * Get the usage, e.g. 'other' or 'filesystem'.
	 * @return The filesystem usage.
	 */
	public function getUsage() {
		$this->getData();
		return $this->usage;
	}

	/**
	 * Get the partition entry information.
	 * @return An array with the fields \em scheme, \em uuid, \em type,
	 *   \em flags, \em number, \em offset, \em size and \em disk,
	 *   otherwise NULL.
	 */
	public function getPartitionEntry() {
		$this->getData();
		return $this->partEntry;
	}

	/**
	 * Get the device path of the filesystem, e.g /dev/sdb1.
	 * @return The device name.
	 */
	public function getDeviceFile() {
		$this->getData();
		return $this->deviceFile;
	}

	/**
	 * Get the device path by UUID, e.g. <ul>
	 * \li /dev/disk/by-uuid/ad3ee177-777c-4ad3-8353-9562f85c0895
	 * \li /dev/disk/by-uuid/2ED43920D438EC29 (NTFS)
	 * </ul>
	 * @return The device path (/dev/disk/by-uuid/xxx).
	 */
	public function getDeviceFileByUuid() {
		$this->getData();
		$this->assertUuid();
		$deviceFile = sprintf("/dev/disk/by-uuid/%s", $this->getUuid());
		if (FALSE === is_block_device($deviceFile)) {
			throw new \OMV\Exception(
			  "Filesystem '%s' does not have a disk/by-uuid device file.",
			  $this->getDeviceFile());
		}
		return $deviceFile;
	}

	/**
	 * Check whether the filesystem has a /dev/disk/by-uuid/xxx device path.
	 * @return Returns TRUE if a disk/by-uuid device path exists,
	 *   otherwise FALSE.
	 */
	public function hasDeviceFileByUuid() {
		try {
			$deviceFile = $this->getDeviceFileByUuid();
		} catch(\Exception $e) {
			return FALSE;
		}
		return is_devicefile_by_uuid($deviceFile);
	}

	/**
	 * Get the device path by ID, e.g. <ul>
	 * \li /dev/disk/by-id/usb-Kingston_DataTraveler_G2_001CC0EC21ADF011C6A20E35-0:0-part1
	 * </ul>
	 * @return The device path (/dev/disk/by-id/xxx).
	 */
	public function getDeviceFileById() {
		$this->getData();
		$deviceFile = parent::getDeviceFileById();
		if (FALSE === is_block_device($deviceFile)) {
			throw new \OMV\Exception(
			  "Filesystem '%s' does not have a disk/by-id device file.",
			  $this->getDeviceFile());
		}
		return $deviceFile;
	}

	/**
	 * Check whether the filesystem has a /dev/disk/by-id/xxx device path.
	 * @return Returns TRUE if a disk/by-id device path exists,
	 *   otherwise FALSE.
	 */
	public function hasDeviceFileById() {
		try {
			$deviceFile = $this->getDeviceFileById();
		} catch(\Exception $e) {
			return FALSE;
		}
		return is_devicefile_by_id($deviceFile);
	}

	/**
	 * Get the device path by label, e.g. <ul>
	 * \li /dev/disk/by-label/data
	 * </ul>
	 * @return The device path (/dev/disk/by-label/xxx).
	 */
	public function getDeviceFileByLabel() {
		$this->getData();
		$this->assertLabel();
		$deviceFile = escape_path(sprintf("/dev/disk/by-label/%s",
			$this->getLabel()));
		if (FALSE === is_block_device($deviceFile)) {
			throw new \OMV\Exception(
			  "Filesystem '%s' does not have a disk/by-label device file.",
			  $this->getDeviceFile());
		}
		return $deviceFile;
	}

	/**
	 * Check whether the filesystem has a /dev/disk/by-label/xxx device path.
	 * @return Returns TRUE if a disk/by-label device path exists,
	 *   otherwise FALSE.
	 */
	public function hasDeviceFileByLabel() {
		try {
			$deviceFile = $this->getDeviceFileByLabel();
		} catch(\Exception $e) {
			return FALSE;
		}
		return is_devicefile_by_label($deviceFile);
	}

	/**
	 * Get the device file of the storage device which contains this
	 * file system.
	 * Example:
	 * <ul>
	 * \li /dev/sdb1 => /dev/sdb
	 * \li /dev/cciss/c0d0p2 => /dev/cciss/c0d0
	 * </ul>
	 * @return The device file of the underlying storage device or NULL
	 *   on failure.
	 */
	public function getParentDeviceFile() {
		$this->getData();
		// The following line is not necessary but will be kept to be safe.
		// If the device file looks like /dev/disk/by-(id|label|path|uuid)/*
		// then it is necessary to get the /dev/xxx equivalent.
		$deviceFile = $this->getCanonicalDeviceFile();
		// Truncate the partition appendix, e.g.:
		// - /dev/sdb1 => /dev/sdb
		// - /dev/cciss/c0d0p2 => /dev/cciss/c0d0
		// - /dev/mapper/vg0-lv0 => /dev/mapper/vg0-lv0
		// - /dev/dm-0 => /dev/dm-0
		// - /dev/md0 => /dev/md0
		// - /dev/loop0 => /dev/loop0
		$mngr = \OMV\System\Storage\Backend\Manager::getInstance();
		if (NULL === ($backend = $mngr->getBackend($deviceFile)))
			return NULL;
		$parentDeviceFile = $backend->baseDeviceFile($deviceFile);
		if (!is_devicefile($parentDeviceFile))
			return NULL;
		// Return the canonical device file.
		return realpath($parentDeviceFile);
	}

	/**
	 * Get the device file in the following order:
	 * <ul>
	 * \li /dev/disk/by-label/xxx
	 * \li /dev/disk/by-id/xxx
	 * \li /dev/disk/by-path/xxx
	 * \li /dev/xxx
	 * </ul>
	 * @return Returns a device file.
	 */
	public function getPredictableDeviceFile() {
		if (TRUE === $this->hasDeviceFileByLabel())
			return $this->getDeviceFileByLabel();
		else if (TRUE === $this->hasDeviceFileById())
			return $this->getDeviceFileById();
		else if (TRUE === $this->hasDeviceFileByPath())
			return $this->getDeviceFileByPath();
		return $this->getCanonicalDeviceFile();
	}

	/**
	 * Get all devices that make up the filesystem.
	 * @return An array that contains the component devices of the filesystem.
	 */
	public function getDeviceFiles() {
		return [ $this->getPreferredDeviceFile() ];
	}

	/**
	 * Check if the filesystem is a multi-device filesystem, e.g. (BTRFS).
	 * @return Returns TRUE if the filesystem has multiple devices,
	 *   otherwise FALSE.
	 */
	public function hasMultipleDevices() {
		$devs = $this->getDeviceFiles();
		if (FALSE === is_array($devs))
			return FALSE;
		return (1 < count($devs));
	}

	/**
	 * Get the filesystem block size.
	 * @return The block size.
	 * @throw \OMV\ExecException
	 */
	public function getBlockSize() {
		$this->getData();
		$cmdArgs = [];
		$cmdArgs[] = "--getbsz";
		$cmdArgs[] = escapeshellarg($this->getDeviceFile());
		$cmd = new \OMV\System\Process("blockdev", $cmdArgs);
		$cmd->setRedirect2to1();
		$cmd->execute($output);
		return intval($output[0]);
	}

	/**
	 * Grow the filesystem.
	 * @return void
	 */
	public function grow() {
		// Nothing to do here.
	}

	/**
	 * Shrink the filesystem.
	 * @return void
	 */
	public function shrink() {
		// Nothing to do here.
	}

	/**
	 * Remove the filesystem.
	 * @return void
	 * @throw \OMV\ExecException
	 */
	public function remove() {
		$this->getData();
		// Whether the partition schema is 'dos' then it is necessary to
		// erase the MBR before, otherwise 'wipefs' fails, e.g.:
		// wipefs: error: /dev/sdh1: appears to contain 'dos' partition table
		if (in_array($this->getPartitionScheme(), [ "dos", "vfat" ])) {
			// http://en.wikipedia.org/wiki/Master_boot_record
			$cmdArgs = [];
			$cmdArgs[] = "if=/dev/zero";
			$cmdArgs[] = sprintf("of=%s", escapeshellarg(
			  $this->getDeviceFile()));
			$cmdArgs[] = "count=1";
			$cmd = new \OMV\System\Process("dd", $cmdArgs);
			$cmd->setRedirect2to1();
			$cmd->execute();
			unset($cmdArgs);
		}
		// Finally remove the filesystem.
		$cmdArgs = [];
		$cmdArgs[] = "-a";
		$cmdArgs[] = escapeshellarg($this->getDeviceFile());
		$cmd = new \OMV\System\Process("wipefs", $cmdArgs);
		$cmd->setRedirect2to1();
		$cmd->execute();
	}

	/**
	 * Get the mount point of the given filesystem.
	 * @return The mountpoint of the filesystem or FALSE.
	 * @throw \OMV\ExecException
	 */
	public function getMountPoint() {
		$this->getData();
		$cmdArgs = [];
		$cmdArgs[] = "-cnrv";
		$cmdArgs[] = "-o SOURCE,TARGET";
		$cmd = new \OMV\System\Process("findmnt", $cmdArgs);
		$cmd->setRedirect2to1();
		$cmd->execute($output);
		// Parse command output:
		// sysfs /sys
		// proc /proc
		// udev /dev
		// devpts /dev/pts
		// tmpfs /run
		// /dev/sda1 /
		// tmpfs /run/lock
		// tmpfs /run/shm
		// tmpfs /tmp
		// /dev/sde1 /media/8c982ec2-8aa7-4fe2-a912-7478f0429e06
		// /dev/sdc1 /media/ea792ccb-cce6-409d-ab9b-0e3fd2bfbfc4
		// /dev/md0 /media/2853402e-fe8f-443b-abd5-c42892f25be1
		// rpc_pipefs /var/lib/nfs/rpc_pipefs
		// /dev/sdf1 /media/7A48-BA97
		// /dev/mapper/vg01-lv01 /srv/_dev_disk_by-id_dm-name-vg01-lv01
		// /dev/sdb1 /srv/dev-disk-by-label-xx\x20yy
		$result = FALSE;
		foreach ($output as $outputk => $outputv) {
			// Split the line into parts.
			$parts = preg_split("/[\s]+/", $outputv);
			// Skip everything that does not contain two parts.
			if (2 !== count($parts))
				continue;
			// Skip everything that does not look like '/dev/xxx'.
			if (FALSE === is_devicefile($parts[0]))
				continue;
			if (realpath($parts[0]) == $this->getCanonicalDeviceFile()) {
				$result = unescape_path($parts[1]);
				break;
			}
		}
		return $result;
	}

	/**
	 * Get statistics from a mounted filesystem.
	 * @return The filesystem statistics if successful, otherwise FALSE. The
	 *   following fields are included: \em devicefile, \em type, \em blocks,
	 *   \em size, \em used, \em available, \em percentage and \em mountpoint.
	 *   Please note, the fields \em size, \em used and \em available are
	 *   strings and their unit is 'B' (bytes).
	 * @throw \OMV\ExecException
	 */
	public function getStatistics() {
		$this->getData();
		// Get the mount point of the filesystem.
		if (FALSE === ($mountPoint = $this->getMountPoint()))
			return FALSE;
		$cmdArgs = [];
		$cmdArgs[] = "-PT";
		$cmdArgs[] = escapeshellarg($mountPoint);
		$cmd = new \OMV\System\Process("df", $cmdArgs);
		$cmd->setRedirect2to1();
		$cmd->execute($output);
		// Parse command output:
		// Filesystem                                             Type     1024-blocks    Used Available Capacity Mounted on
		// rootfs                                                 rootfs      14801380 1392488  12657020      10% /
		// udev                                                   devtmpfs       10240       0     10240       0% /dev
		// tmpfs                                                  tmpfs         102356     324    102032       1% /run
		// /dev/disk/by-uuid/128917b7-718a-4361-ab6f-6b1725564cb8 ext4        14801380 1392488  12657020      10% /
		// tmpfs                                                  tmpfs           5120       0      5120       0% /run/lock
		// tmpfs                                                  tmpfs         342320       0    342320       0% /run/shm
		// tmpfs                                                  tmpfs         511768       4    511764       1% /tmp
		// /dev/sdb1                                              ext4          100156    4164     95992       5% /media/b994e5e8-94ae-482c-ae54-70a0bbb2737e
		// /dev/sdc1                                              ext4         1014660    2548    995728       1% /srv/dev-disk-by-label-xx yy
		$result = FALSE;
		foreach ($output as $outputk => $outputv) {
			$regex = '/^(\S+)\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)%\s+(.+)$/i';
			if (1 !== preg_match($regex, $outputv, $matches))
				continue;
			if (0 !== strcasecmp($mountPoint, $matches[7]))
				continue;
			$result = [
				"devicefile" => $this->deviceFile,
				"type" => $matches[2],
				"blocks" => $matches[3],
				"size" => bcmul($matches[3], "1024", 0),
				"used" => binary_convert($matches[4], "KiB", "B"),
				"available" => binary_convert($matches[5], "KiB", "B"),
				"percentage" => intval(trim($matches[6], "%")),
				"mountpoint" => $matches[7]
			];
			break;
		}
		return $result;
	}

	/**
	 * Check if a filesystem is mounted.
	 * @return TRUE if the filesystem is mounted, otherwise FALSE.
	 * @throw \OMV\ExecException
	 */
	public function isMounted() {
		$this->getData();
		$cmdArgs = [];
		$cmdArgs[] = "-cnrv";
		$cmdArgs[] = "-o SOURCE";
		$cmd = new \OMV\System\Process("findmnt", $cmdArgs);
		$cmd->setRedirect2to1();
		$cmd->execute($output);
		// Parse command output:
		// sysfs
		// proc
		// udev
		// devpts
		// tmpfs
		// /dev/sda1
		// /dev/sde1
		// /dev/sdc1
		// /dev/md0
		// /dev/sdf1
		// /dev/mapper/vg01-lv01
		$result = FALSE;
		foreach ($output as $outputk => $outputv) {
			// Skip everything that does not look like '/dev/xxx'.
			if (FALSE === is_devicefile($outputv))
				continue;
			// Compare the device files.
			if (realpath($outputv) == $this->getCanonicalDeviceFile()) {
				$result = TRUE;
				break;
			}
		}
		return $result;
	}

	/**
	 * Mount the filesystem by its device file or UUID.
	 * @param options Additional mount options as array or string.
	 *   Defaults to "".
	 * @param dir The optional mount directory. Defaults to "".
	 * @return void
	 * @throw \OMV\ExecException
	 */
	public function mount($options = "", $dir = "") {
		$cmdArgs = [];
		$cmdArgs[] = "-v";
		if (!empty($options)) {
			if (!is_array($options))
				$options = [ $options ];
			$cmdArgs[] = sprintf("-o %s", implode(",", $options));
		}
		//if (TRUE === $this->hasUuid())
		//	$cmdArgs[] = sprintf("-U %s", $this->getUuid());
		//else
		//	$cmdArgs[] = sprintf("--source %s", escapeshellarg(
		//		$this->getDeviceFile()));
		$cmdArgs[] = sprintf("--source %s", escapeshellarg(
			$this->getPredictableDeviceFile()));
		if (!empty($dir))
			$cmdArgs[] = escapeshellarg($dir);
		$cmd = new \OMV\System\Process("mount", $cmdArgs);
		$cmd->setRedirect2to1();
		$cmd->execute();
	}

	/**
	 * Unmount the filesystem.
	 * @param force Set to TRUE to force unmount. Defaults to FALSE.
	 * @param lazy Set to TRUE to lazy unmount. Defaults to FALSE.
	 * @return void
	 * @throw \OMV\ExecException
	 */
	public function umount($force = FALSE, $lazy = FALSE) {
		$cmdArgs = [];
		$cmdArgs[] = "-v";
		if (TRUE === $force)
			$cmdArgs[] = "-f";
		if (TRUE === $lazy)
			$cmdArgs[] = "-l";
		$cmdArgs[] = escapeshellarg($this->getPredictableDeviceFile());
		$cmd = new \OMV\System\Process("umount", $cmdArgs);
		$cmd->setRedirect2to1();
		$cmd->execute();
	}

	/**
	 * Set the backend of this filesystem.
	 */
	final public function setBackend(Backend\BackendAbstract $backend) {
		$this->backend = $backend;
	}

	/**
	 * Get the backend of this filesystem.
	 */
	final public function getBackend() {
		return $this->backend;
	}

	/**
	 * Get the description of the file system.
	 * @return The file system description.
	 */
	public function getDescription() {
		$this->getData();
		// Use the device file as default.
		$result = $this->getDeviceFile();
		// If the file system has a label, then use this as an alternative.
		if (TRUE === $this->hasLabel())
			$result = $this->getLabel();
		// Try to get the file system statistics to get a more detailed
		// description.
		if (FALSE !== ($stats = $this->getStatistics())) {
			$result = sprintf(gettext("%s [%s (%d%%) used, %s available]"),
			  $result, binary_format($stats['used']),
			  $stats['percentage'], binary_format($stats['available']));
		}
		return $result;
	}

	/**
	 * Check if the given device file contains a file system.
	 * @param deviceFile The devicefile to check, e.g. /dev/sda.
	 * @return TRUE if the devicefile has a file system, otherwise FALSE.
	 */
	public static function hasFileSystem($deviceFile) {
		$cmdArgs = [];
		$cmdArgs[] = "|";
		$cmdArgs[] = "grep";
		$cmdArgs[] = "-E";
		$cmdArgs[] = escapeshellarg(sprintf("^%s.*:.+\sTYPE=.+$",
		  realpath($deviceFile)));
		$cmd = new \OMV\System\Process("blkid", $cmdArgs);
		$cmd->setQuiet(TRUE);
		$cmd->execute($output, $exitStatus);
		if ($exitStatus !== 0)
			return FALSE;
		return TRUE;
	}

	/**
	 * Get all available/detected filesystems.
	 * @return An array of \ref Filesystem objects.
	 * @throw \OMV\Exception
	 */
	public static function getFilesystems() {
		$result = [];
		$mngr = Backend\Manager::getInstance();
		foreach ($mngr->enumerate() as $datak => $datav) {
			// Get the filesystem backend.
			$mngr->assertBackendExistsByType($datav['type']);
			$backend = $mngr->getBackendByType($datav['type']);
			// Get the filesystem implementation. Use the devicefile to get
			// the filesystem details because VFAT filesystems do not have
			// a valid UUID.
			$fs = $backend->getImpl($datav['devicefile']);
			if (is_null($fs) || !$fs->exists()) {
				throw new \OMV\Exception(
				  "Failed to get the '%s' filesystem implementation ".
				  "or '%s' does not exist.", $backend->getType(),
				  $datav['devicefile']);
			}
			$result[] = $fs;
		}
		return $result;
	}

	/**
	 * Get the object of the class which implements the specified filesystem
	 * for the given filesystem UUID or device path.
	 * @param id The UUID or device path of the filesystem, e.g.
	 *   <ul>
	 *   \li 78b669c1-9183-4ca3-a32c-80a4e2c61e2d (EXT2/3/4, JFS, XFS)
	 *   \li 7A48-BA97 (FAT)
	 *   \li 2ED43920D438EC29 (NTFS)
	 *   \li /dev/sde1
	 *   \li /dev/disk/by-id/scsi-SATA_ST3200XXXX2AS_5XWXXXR6-part1
	 *   \li /dev/disk/by-label/DATA
	 *   \li /dev/disk/by-path/pci-0000:00:10.0-scsi-0:0:0:0-part2
	 *   \li /dev/disk/by-uuid/ad3ee177-777c-4ad3-8353-9562f85c0895
	 *   \li /dev/cciss/c0d0p2
	 *   </ul>
	 * @return The object of the class implementing the given filesystem,
	 *   otherwise NULL.
	 */
	public static function getImpl($id) {
		$mngr = Backend\Manager::getInstance();
		if (NULL == ($backend = $mngr->getBackendById($id)))
			return NULL;
		return $backend->getImpl($id);
	}

	/**
	 * @throw \OMV\AssertException
	 */
	public static function assertGetImpl($id) {
		$result = self::getImpl($id);
		if (is_null($result) || !$result->exists()) {
			throw new \OMV\Exception(
			  "Failed to get the filesystem implementation for '%s' ".
			  "or the filesystem does not exist.", $id);
		}
	}

	/**
	 * Get the object of the class which implements the specified filesystem
	 * for the given mount point.
	 * @param dir The file system path prefix, e.g. '/home/ftp/data'.
	 * @return The class object or NULL on failure.
	 */
	public static function getImplByMountPoint($dir) {
		if (TRUE === empty($dir))
			return NULL;
		$result = NULL;
		foreach (file("/proc/mounts") as $rowk => $rowv) {
			// Parse command output:
			// rootfs / rootfs rw 0 0
			// none /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0
			// none /proc proc rw,nosuid,nodev,noexec,relatime 0 0
			// tmpfs /dev/shm tmpfs rw,nosuid,nodev,relatime 0 0
			// /dev/sdb2 /home/ftp/data ext4 rw,noexec,relatime,user_xattr,acl,barrier=1,data=ordered,jqfmt=vfsv0,usrjquota=aquota.user,grpjquota=aquota.group 0 0
			// /dev/sde1 /media/46607e80-dc8e-427e-9939-7909985dfe5b ext4 rw,noexec,relatime,user_xattr,acl,barrier=1,data=ordered,jqfmt=vfsv0,usrjquota=aquota.user,grpjquota=aquota.group 0 0
			$parts = explode(" ", $rowv);
			if ($parts[1] == escape_path($dir, TRUE)) {
				// Get the backend by the file system type (e.g. ext4, xfs,
				// nfs, aufs, ...).
				$mngr = Backend\Manager::getInstance();
				if (NULL !== ($backend = $mngr->getBackendByType($parts[2])))
					$result = $backend->getImpl($parts[0]);
				break;
			}
		}
		return $result;
	}
}
